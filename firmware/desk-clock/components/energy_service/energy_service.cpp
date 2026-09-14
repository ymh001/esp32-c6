#include "energy_service.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "energy_credentials.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "energy";

#define ENERGY_REFRESH_BIT BIT0
#define ENERGY_RESPONSE_MAX 16384

static SemaphoreHandle_t s_snapshot_mutex;
static EventGroupHandle_t s_events;
static energy_snapshot_t s_snapshot;

static void set_message(const char *message)
{
    xSemaphoreTake(s_snapshot_mutex, portMAX_DELAY);
    strlcpy(s_snapshot.message, message, sizeof(s_snapshot.message));
    xSemaphoreGive(s_snapshot_mutex);
}

static void make_timestamp(char *out, size_t size)
{
    time_t now = time(NULL);
    struct tm local = {};
    localtime_r(&now, &local);
    strftime(out, size, "%Y%m%d%H%M%S", &local);
    strlcat(out, "000", size);
}

static void make_year_month(char *out, size_t size)
{
    time_t now = time(NULL);
    struct tm local = {};
    localtime_r(&now, &local);
    strftime(out, size, "%Y-%m", &local);
}

static char *url_encode(const char *input)
{
    static const char hex[] = "0123456789ABCDEF";
    const size_t length = strlen(input);
    char *encoded = (char *)malloc(length * 3 + 1);
    if (encoded == NULL) {
        return NULL;
    }

    size_t out = 0;
    for (size_t i = 0; i < length; ++i) {
        const unsigned char c = (unsigned char)input[i];
        const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                          c == '.' || c == '~';
        if (safe) {
            encoded[out++] = (char)c;
        } else {
            encoded[out++] = '%';
            encoded[out++] = hex[c >> 4];
            encoded[out++] = hex[c & 0x0F];
        }
    }
    encoded[out] = '\0';
    return encoded;
}

static esp_err_t post_form(const char *method, const char *param,
                           char **response_out)
{
    *response_out = NULL;
    char *encoded_param = url_encode(param);
    if (encoded_param == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const size_t body_capacity =
        strlen(encoded_param) + strlen(method) + strlen(ENERGY_CUSTOMER_CODE) +
        strlen(ENERGY_COMMAND) + 64;
    char *body = (char *)calloc(1, body_capacity);
    if (body == NULL) {
        free(encoded_param);
        return ESP_ERR_NO_MEM;
    }
    snprintf(body, body_capacity,
             "param=%s&customercode=%s&method=%s&command=%s", encoded_param,
             ENERGY_CUSTOMER_CODE, method, ENERGY_COMMAND);
    free(encoded_param);

    const esp_http_client_config_t config = {
        .url = ENERGY_API_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 45000,
        .buffer_size = 2048,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        free(body);
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_set_header(
        client, "Content-Type", "application/x-www-form-urlencoded");
    if (err == ESP_OK) {
        err = esp_http_client_open(client, strlen(body));
    }
    if (err == ESP_OK) {
        const int written = esp_http_client_write(client, body, strlen(body));
        if (written != (int)strlen(body)) {
            err = ESP_FAIL;
        }
    }
    free(body);

    int64_t content_length = -1;
    if (err == ESP_OK) {
        content_length = esp_http_client_fetch_headers(client);
        if (content_length <= 0 || content_length > ENERGY_RESPONSE_MAX) {
            err = ESP_ERR_INVALID_SIZE;
        }
    }

    char *response = NULL;
    if (err == ESP_OK) {
        response = (char *)calloc(1, (size_t)content_length + 1);
        if (response == NULL) {
            err = ESP_ERR_NO_MEM;
        }
    }

    size_t received = 0;
    while (err == ESP_OK && received < (size_t)content_length) {
        const int result =
            esp_http_client_read(client, response + received,
                                 (int)((size_t)content_length - received));
        if (result <= 0) {
            err = ESP_FAIL;
            break;
        }
        received += (size_t)result;
    }
    if (err == ESP_OK) {
        response[received] = '\0';
        if (esp_http_client_get_status_code(client) != 200) {
            err = ESP_FAIL;
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK) {
        free(response);
        return err;
    }

    *response_out = response;
    return ESP_OK;
}

static cJSON *parse_response_body(char *response)
{
    cJSON *root = cJSON_Parse(response);
    if (root == NULL) {
        return NULL;
    }
    const cJSON *body = cJSON_GetObjectItemCaseSensitive(root, "body");
    if (!cJSON_IsString(body) || body->valuestring == NULL) {
        cJSON_Delete(root);
        return NULL;
    }
    cJSON *parsed = cJSON_Parse(body->valuestring);
    cJSON_Delete(root);
    return parsed;
}

static float json_number(const cJSON *object, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsNumber(item)) {
        return (float)item->valuedouble;
    }
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        return strtof(item->valuestring, NULL);
    }
    return NAN;
}

static bool is_electricity_meter(const cJSON *meter)
{
    const cJSON *accode = cJSON_GetObjectItemCaseSensitive(meter, "accode");
    if (cJSON_IsNumber(accode)) {
        const int code = accode->valueint;
        return code == 51101 || code == 51102;
    }
    if (cJSON_IsString(accode) && accode->valuestring != NULL) {
        return strncmp(accode->valuestring, "51101", 5) == 0 ||
               strncmp(accode->valuestring, "51102", 5) == 0;
    }
    return false;
}

static esp_err_t refresh_summary(void)
{
    char timestamp[24];
    char param[256];
    make_timestamp(timestamp, sizeof(timestamp));
    snprintf(param, sizeof(param),
             "{\"cmd\":\"h5_getstuindexpage\",\"roomverify\":\"%s\","
             "\"account\":\"%s\",\"timestamp\":\"%s\"}",
             ENERGY_ROOM_VERIFY, ENERGY_ACCOUNT, timestamp);

    char *response = NULL;
    esp_err_t err = post_form("h5_getstuindexpage", param, &response);
    if (err != ESP_OK) {
        return err;
    }

    cJSON *root = parse_response_body(response);
    free(response);
    if (root == NULL) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    const cJSON *meters = cJSON_GetObjectItemCaseSensitive(root, "modlist");
    const cJSON *meter = NULL;
    if (cJSON_IsArray(meters)) {
        cJSON *candidate = NULL;
        cJSON_ArrayForEach(candidate, meters) {
            if (is_electricity_meter(candidate)) {
                meter = candidate;
                break;
            }
        }
    }
    if (meter == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }

    float week_total = 0.0f;
    const cJSON *week = cJSON_GetObjectItemCaseSensitive(meter, "weekuselist");
    const cJSON *day = NULL;
    if (cJSON_IsArray(week)) {
        cJSON_ArrayForEach(day, week) {
            const float value = json_number(day, "use");
            if (!isnan(value)) {
                week_total += value;
            }
        }
    }

    xSemaphoreTake(s_snapshot_mutex, portMAX_DELAY);
    s_snapshot.today_kwh = json_number(meter, "todayuse");
    s_snapshot.week_kwh = week_total;
    s_snapshot.remaining_kwh = json_number(meter, "odd");
    s_snapshot.loaded = true;
    strlcpy(s_snapshot.message, "使用数据已更新", sizeof(s_snapshot.message));
    xSemaphoreGive(s_snapshot_mutex);
    ESP_LOGI(TAG, "Electricity today=%.2f week=%.2f remaining=%.2f kWh",
             s_snapshot.today_kwh, s_snapshot.week_kwh,
             s_snapshot.remaining_kwh);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t refresh_month_from_daily(void)
{
    char timestamp[24];
    char year_month[8];
    char param[320];
    make_timestamp(timestamp, sizeof(timestamp));
    make_year_month(year_month, sizeof(year_month));
    snprintf(param, sizeof(param),
             "{\"cmd\":\"getusedetail\",\"roomverify\":\"%s\","
             "\"businesstype\":%d,\"yearmonth\":\"%s\",\"account\":\"%s\","
             "\"timestamp\":\"%s\"}",
             ENERGY_ROOM_VERIFY, ENERGY_ELECTRICITY_BUSINESS_TYPE,
             year_month, ENERGY_ACCOUNT, timestamp);

    char *response = NULL;
    esp_err_t err = post_form("getusedetail", param, &response);
    if (err != ESP_OK) {
        return err;
    }

    cJSON *root = parse_response_body(response);
    free(response);
    if (root == NULL) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    const cJSON *days = cJSON_GetObjectItemCaseSensitive(root, "dayuselist");
    if (!cJSON_IsArray(days)) {
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }

    float month_total = 0.0f;
    const cJSON *day = NULL;
    cJSON_ArrayForEach(day, days) {
        float value = json_number(day, "dayuse");
        if (isnan(value)) {
            value = json_number(day, "use");
        }
        if (!isnan(value)) {
            month_total += value;
        }
    }

    xSemaphoreTake(s_snapshot_mutex, portMAX_DELAY);
    s_snapshot.month_kwh = month_total;
    s_snapshot.month_loaded = true;
    xSemaphoreGive(s_snapshot_mutex);
    ESP_LOGI(TAG, "Electricity month(sum)=%.2f kWh from %u daily entries",
             month_total, (unsigned)cJSON_GetArraySize(days));
    cJSON_Delete(root);
    return ESP_OK;
}

static void refresh_all(void)
{
    xSemaphoreTake(s_snapshot_mutex, portMAX_DELAY);
    s_snapshot.refreshing = true;
    strlcpy(s_snapshot.message, "正在更新", sizeof(s_snapshot.message));
    xSemaphoreGive(s_snapshot_mutex);

    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < 2; ++attempt) {
        err = refresh_summary();
        if (err == ESP_OK) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1500));
    }

    xSemaphoreTake(s_snapshot_mutex, portMAX_DELAY);
    s_snapshot.refreshing = false;
    if (err == ESP_OK) {
        strlcpy(s_snapshot.message, "日/周数据已更新",
                sizeof(s_snapshot.message));
    }
    xSemaphoreGive(s_snapshot_mutex);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Energy refresh failed: %s", esp_err_to_name(err));
        char message[64];
        snprintf(message, sizeof(message), "更新失败: %s",
                 esp_err_to_name(err));
        set_message(message);
        return;
    }

    xSemaphoreTake(s_snapshot_mutex, portMAX_DELAY);
    s_snapshot.month_refreshing = true;
    xSemaphoreGive(s_snapshot_mutex);
    const esp_err_t month_error = refresh_month_from_daily();
    xSemaphoreTake(s_snapshot_mutex, portMAX_DELAY);
    s_snapshot.month_refreshing = false;
    if (month_error == ESP_OK) {
        strlcpy(s_snapshot.message, "本月已按每日数据汇总",
                sizeof(s_snapshot.message));
    } else {
        strlcpy(s_snapshot.message, "本月汇总待更新",
                sizeof(s_snapshot.message));
    }
    xSemaphoreGive(s_snapshot_mutex);
    if (month_error != ESP_OK) {
        ESP_LOGW(TAG, "Month daily sum failed: %s",
                 esp_err_to_name(month_error));
    }
}

static void energy_task(void *arg)
{
    (void)arg;
    for (;;) {
        refresh_all();
        xEventGroupWaitBits(
            s_events, ENERGY_REFRESH_BIT, pdTRUE, pdFALSE,
            pdMS_TO_TICKS(ENERGY_REFRESH_INTERVAL_SECONDS * 1000));
    }
}

esp_err_t energy_service_start(void)
{
    s_snapshot_mutex = xSemaphoreCreateMutex();
    s_events = xEventGroupCreate();
    if (s_snapshot_mutex == NULL || s_events == NULL) {
        return ESP_ERR_NO_MEM;
    }
    strlcpy(s_snapshot.message, "等待更新", sizeof(s_snapshot.message));

    if (xTaskCreate(energy_task, "energy", 12288, NULL, 4, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void energy_service_request_refresh(void)
{
    if (s_events != NULL) {
        xEventGroupSetBits(s_events, ENERGY_REFRESH_BIT);
    }
}

void energy_service_get_snapshot(energy_snapshot_t *snapshot)
{
    if (snapshot == NULL || s_snapshot_mutex == NULL) {
        return;
    }
    xSemaphoreTake(s_snapshot_mutex, portMAX_DELAY);
    *snapshot = s_snapshot;
    xSemaphoreGive(s_snapshot_mutex);
}
