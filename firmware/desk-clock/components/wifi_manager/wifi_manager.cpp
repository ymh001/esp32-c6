#include "wifi_manager.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi_credentials.h"

static const char *TAG = "wifi";

#define RECONNECT_MIN_DELAY_MS 1000
#define RECONNECT_MAX_DELAY_MS 10000

static esp_netif_t *s_sta_netif;
static esp_timer_handle_t s_reconnect_timer;
static volatile wifi_manager_state_t s_state = WIFI_MANAGER_IDLE;
static volatile bool s_connect_requested;
static volatile int s_retry_count;
static char s_ip[16] = "0.0.0.0";

static void set_state(wifi_manager_state_t state)
{
    s_state = state;
}

static uint32_t reconnect_delay_ms(void)
{
    uint32_t delay = (uint32_t)(s_retry_count > 0 ? s_retry_count : 1) *
                     RECONNECT_MIN_DELAY_MS;
    if (delay > RECONNECT_MAX_DELAY_MS) {
        delay = RECONNECT_MAX_DELAY_MS;
    }
    return delay;
}

static void reconnect_timer_callback(void *arg)
{
    (void)arg;
    if (s_connect_requested) {
        esp_wifi_connect();
    }
}

static void schedule_reconnect(void)
{
    if (!s_connect_requested || s_reconnect_timer == NULL) {
        return;
    }
    esp_timer_stop(s_reconnect_timer);
    const uint32_t delay = reconnect_delay_ms();
    ESP_LOGW(TAG, "Reconnecting in %u ms", (unsigned)delay);
    esp_timer_start_once(s_reconnect_timer, (uint64_t)delay * 1000);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (s_connect_requested) {
            set_state(WIFI_MANAGER_CONNECTING);
            esp_wifi_connect();
        }
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event =
            (const wifi_event_sta_disconnected_t *)event_data;
        if (!s_connect_requested) {
            return;
        }
        s_retry_count = s_retry_count + 1;
        set_state(WIFI_MANAGER_CONNECTING);
        ESP_LOGW(TAG, "Disconnected from '%s' (reason %u)",
                 DESK_CLOCK_WIFI_SSID, event->reason);
        schedule_reconnect();
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_count = 0;
        esp_timer_stop(s_reconnect_timer);
        set_state(WIFI_MANAGER_CONNECTED);
        ESP_LOGI(TAG, "Connected to '%s', IP: %s", DESK_CLOCK_WIFI_SSID,
                 s_ip);
    }
}

static esp_err_t initialize_wifi(void)
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (s_sta_netif == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_netif_set_hostname(s_sta_netif, "desk-clock");

    const wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                     wifi_event_handler, NULL);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                     wifi_event_handler, NULL);
    if (err != ESP_OK) {
        return err;
    }

    const esp_timer_create_args_t timer_args = {
        .callback = reconnect_timer_callback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "wifi_reconnect",
        .skip_unhandled_events = false,
    };
    return esp_timer_create(&timer_args, &s_reconnect_timer);
}

static void connect_task(void *arg)
{
    (void)arg;

    wifi_config_t config = {};
    strlcpy((char *)config.sta.ssid, DESK_CLOCK_WIFI_SSID,
            sizeof(config.sta.ssid));
    strlcpy((char *)config.sta.password, DESK_CLOCK_WIFI_PASSWORD,
            sizeof(config.sta.password));
    config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;

    ESP_LOGI(TAG, "Connecting to hardcoded Wi-Fi SSID '%s'",
             DESK_CLOCK_WIFI_SSID);
    s_connect_requested = true;
    set_state(WIFI_MANAGER_CONNECTING);

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_STA, &config);
    }
    if (err == ESP_OK) {
        err = esp_wifi_start();
    }
    if (err != ESP_OK) {
        set_state(WIFI_MANAGER_ERROR);
        ESP_LOGE(TAG, "Wi-Fi startup failed: %s", esp_err_to_name(err));
    }

    vTaskDelete(NULL);
}

esp_err_t wifi_manager_start(void)
{
    esp_err_t err = initialize_wifi();
    if (err != ESP_OK) {
        set_state(WIFI_MANAGER_ERROR);
        ESP_LOGE(TAG, "Wi-Fi initialization failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    if (xTaskCreate(connect_task, "wifi_connect", 4096, NULL, 5, NULL) !=
        pdPASS) {
        set_state(WIFI_MANAGER_ERROR);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

wifi_manager_state_t wifi_manager_state(void)
{
    return s_state;
}

bool wifi_manager_is_connected(void)
{
    return s_state == WIFI_MANAGER_CONNECTED;
}

void wifi_manager_get_ip(char *out, size_t size)
{
    if (out == NULL || size == 0) {
        return;
    }
    strlcpy(out, s_ip, size);
}
