#include "wifi_manager.h"
#include "time_service.h"

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "board.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "time";
static portMUX_TYPE s_sync_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_synced;
static bool s_rtc_write_pending;
static int64_t s_last_sync;
static clock_settings_t s_settings;
static bool s_initialized;
static bool s_sntp_started;

static void write_system_time_to_rtc(void)
{
    time_t now = time(NULL);
    struct tm local = {0};
    localtime_r(&now, &local);
    esp_err_t err = board_rtc_write(&local);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to update RTC: %s", esp_err_to_name(err));
    }
}

static void sntp_sync_callback(struct timeval *tv)
{
    portENTER_CRITICAL(&s_sync_lock);
    s_synced = true;
    s_last_sync = (int64_t)tv->tv_sec;
    s_rtc_write_pending = true;
    portEXIT_CRITICAL(&s_sync_lock);
    ESP_LOGI(TAG, "Time synchronized");
}

void time_service_apply_timezone(const char *timezone)
{
    if (timezone == NULL || timezone[0] == '\0') {
        timezone = "CST-8";
    }
    setenv("TZ", timezone, 1);
    tzset();
}

esp_err_t time_service_init(const clock_settings_t *settings)
{
    if (settings == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_settings = *settings;
    time_service_apply_timezone(settings->timezone);

    struct tm rtc_time = {0};
    esp_err_t err = board_rtc_read(&rtc_time);
    if (err == ESP_OK) {
        const time_t value = mktime(&rtc_time);
        if (value > 0) {
            const struct timeval tv = {.tv_sec = value};
            settimeofday(&tv, NULL);
        }
    } else {
        ESP_LOGW(TAG, "RTC unavailable: %s", esp_err_to_name(err));
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Waiting for Wi-Fi before starting SNTP");
    return ESP_OK;
}

esp_err_t time_service_start(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_sntp_started) {
        return ESP_OK;
    }

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        2, ESP_SNTP_SERVER_LIST(s_settings.ntp_server_1,
                                s_settings.ntp_server_2));
    config.sync_cb = sntp_sync_callback;
    config.start = true;
    const esp_err_t err = esp_netif_sntp_init(&config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SNTP init failed: %s", esp_err_to_name(err));
        return err;
    }

    s_sntp_started = true;
    ESP_LOGI(TAG, "SNTP started with %s and %s", s_settings.ntp_server_1,
             s_settings.ntp_server_2);
    return ESP_OK;
}

bool time_service_is_synced(void)
{
    portENTER_CRITICAL(&s_sync_lock);
    const bool synced = s_synced;
    portEXIT_CRITICAL(&s_sync_lock);
    return synced;
}

int64_t time_service_last_sync_epoch(void)
{
    portENTER_CRITICAL(&s_sync_lock);
    const int64_t last_sync = s_last_sync;
    portEXIT_CRITICAL(&s_sync_lock);
    return last_sync;
}

void time_service_get_local(struct tm *out)
{
    time_t now = time(NULL);
    localtime_r(&now, out);
}

void time_service_process(void)
{
    static bool was_connected;
    const bool connected=wifi_manager_is_connected();
    if(connected && !was_connected && s_sntp_started)esp_netif_sntp_start();
    was_connected=connected;
    portENTER_CRITICAL(&s_sync_lock);
    const bool pending = s_rtc_write_pending;
    s_rtc_write_pending = false;
    portEXIT_CRITICAL(&s_sync_lock);
    if (pending) write_system_time_to_rtc();
}
