#include "clock_settings.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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
    settings->sync_minutes = 15;
    settings->screen_off_seconds = 30;
    settings->stay_awake_on_power = 1;
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
    nvs_get_u8(handle, "sync_min", &settings->sync_minutes);
    nvs_get_u8(handle, "screen_off", &settings->screen_off_seconds);
    nvs_get_u8(handle, "plug_awake", &settings->stay_awake_on_power);
    nvs_get_u8(handle, "use24h", &settings->use_24_hour);
    nvs_get_u8(handle, "brightness", &settings->brightness);
    nvs_get_u8(handle, "night_bright", &settings->night_brightness);
    nvs_get_u8(handle, "night_start", &settings->night_start_hour);
    nvs_get_u8(handle, "night_end", &settings->night_end_hour);

    nvs_close(handle);

    const unsigned sync = settings->sync_minutes, off = settings->screen_off_seconds;
    if(sync!=0 && sync!=1 && sync!=5 && sync!=15 && sync!=30 && sync!=60)settings->sync_minutes=15;
    if(off!=0 && off!=5 && off!=15 && off!=30 && off!=60)settings->screen_off_seconds=30;
    settings->stay_awake_on_power=!!settings->stay_awake_on_power;
    settings->use_24_hour = 1; // Migrate any saved 12-hour preference.
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
    if (err == ESP_OK) err = nvs_set_u8(handle, "sync_min", settings->sync_minutes);
    if (err == ESP_OK) err = nvs_set_u8(handle, "screen_off", settings->screen_off_seconds);
    if (err == ESP_OK) err = nvs_set_u8(handle, "plug_awake", settings->stay_awake_on_power);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

// UI callbacks only publish a copy. NVS writes run from app_main without
// holding the LVGL lock, and repeated changes collapse to the newest value.
static portMUX_TYPE s_pending_lock = portMUX_INITIALIZER_UNLOCKED;
static clock_settings_t s_pending_settings;
static bool s_save_pending;

void clock_settings_request_save(const clock_settings_t *settings)
{
    if (settings == NULL) return;
    portENTER_CRITICAL(&s_pending_lock);
    s_pending_settings = *settings;
    s_save_pending = true;
    portEXIT_CRITICAL(&s_pending_lock);
}

void clock_settings_process(void)
{
    static TickType_t last_attempt;
    const TickType_t now = xTaskGetTickCount();
    if (now - last_attempt < pdMS_TO_TICKS(1000)) return;
    clock_settings_t pending;
    portENTER_CRITICAL(&s_pending_lock);
    const bool save = s_save_pending;
    if (save) {
        pending = s_pending_settings;
        s_save_pending = false;
    }
    portEXIT_CRITICAL(&s_pending_lock);
    if (!save) return;
    last_attempt = now;
    const esp_err_t err = clock_settings_save(&pending);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Settings save failed, will retry: %s", esp_err_to_name(err));
        portENTER_CRITICAL(&s_pending_lock);
        if (!s_save_pending) {
            s_pending_settings = pending;
            s_save_pending = true;
        }
        portEXIT_CRITICAL(&s_pending_lock);
    }
}
