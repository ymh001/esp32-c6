#include "clock_settings.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "settings";
static const char *NVS_NAMESPACE = "deskclock";

static esp_err_t load_string(nvs_handle_t handle, const char *key, char *value,
                             size_t size)
{
    size_t required = size;
    return nvs_get_str(handle, key, value, &required);
}

void clock_settings_defaults(clock_settings_t *settings)
{
    memset(settings, 0, sizeof(*settings));
    strlcpy(settings->timezone, "CST-8", sizeof(settings->timezone));
    strlcpy(settings->ntp_server_1, "ntp.aliyun.com",
            sizeof(settings->ntp_server_1));
    strlcpy(settings->ntp_server_2, "time.cloudflare.com",
            sizeof(settings->ntp_server_2));
    settings->use_24_hour = 1;
    settings->brightness = 80;
    settings->night_brightness = 20;
    settings->night_start_hour = 22;
    settings->night_end_hour = 7;
}

esp_err_t clock_settings_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t clock_settings_load(clock_settings_t *settings)
{
    if (settings == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    clock_settings_defaults(settings);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    load_string(handle, "timezone", settings->timezone,
                sizeof(settings->timezone));
    load_string(handle, "ntp1", settings->ntp_server_1,
                sizeof(settings->ntp_server_1));
    load_string(handle, "ntp2", settings->ntp_server_2,
                sizeof(settings->ntp_server_2));
    nvs_get_u8(handle, "use24h", &settings->use_24_hour);
    nvs_get_u8(handle, "brightness", &settings->brightness);
    nvs_get_u8(handle, "night_bright", &settings->night_brightness);
    nvs_get_u8(handle, "night_start", &settings->night_start_hour);
    nvs_get_u8(handle, "night_end", &settings->night_end_hour);

    nvs_close(handle);

    settings->use_24_hour = settings->use_24_hour != 0;
    if (settings->brightness < 10 || settings->brightness > 100) {
        settings->brightness = 80;
    }
    if (settings->night_brightness < 10 || settings->night_brightness > 100) {
        settings->night_brightness = 20;
    }
    if (settings->night_start_hour > 23) {
        settings->night_start_hour = 22;
    }
    if (settings->night_end_hour > 23) {
        settings->night_end_hour = 7;
    }
    if (settings->timezone[0] == '\0') {
        strlcpy(settings->timezone, "CST-8", sizeof(settings->timezone));
    }
    if (settings->ntp_server_1[0] == '\0') {
        strlcpy(settings->ntp_server_1, "ntp.aliyun.com",
                sizeof(settings->ntp_server_1));
    }
    if (settings->ntp_server_2[0] == '\0') {
        strlcpy(settings->ntp_server_2, "time.cloudflare.com",
                sizeof(settings->ntp_server_2));
    }
    return ESP_OK;
}

esp_err_t clock_settings_save(const clock_settings_t *settings)
{
    if (settings == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(
        nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle), TAG, "NVS open");

    esp_err_t err = nvs_set_str(handle, "timezone", settings->timezone);
    if (err == ESP_OK) err = nvs_set_str(handle, "ntp1", settings->ntp_server_1);
    if (err == ESP_OK) err = nvs_set_str(handle, "ntp2", settings->ntp_server_2);
    if (err == ESP_OK) err = nvs_set_u8(handle, "use24h", settings->use_24_hour);
    if (err == ESP_OK) err = nvs_set_u8(handle, "brightness", settings->brightness);
    if (err == ESP_OK) err = nvs_set_u8(handle, "night_bright", settings->night_brightness);
    if (err == ESP_OK) err = nvs_set_u8(handle, "night_start", settings->night_start_hour);
    if (err == ESP_OK) err = nvs_set_u8(handle, "night_end", settings->night_end_hour);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
