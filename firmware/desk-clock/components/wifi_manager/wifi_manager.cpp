#include "wifi_manager.h"

#include <atomic>
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
#define CONNECT_FAILOVER_THRESHOLD 3

typedef struct {
    const char *ssid;
    const char *password;
} wifi_network_t;

static const wifi_network_t s_networks[] = {
    {
        .ssid = DESK_CLOCK_WIFI_PRIMARY_SSID,
        .password = DESK_CLOCK_WIFI_PRIMARY_PASSWORD,
    },
    {
        .ssid = DESK_CLOCK_WIFI_BACKUP_SSID,
        .password = DESK_CLOCK_WIFI_BACKUP_PASSWORD,
    },
    {
        .ssid = DESK_CLOCK_WIFI_THIRD_SSID,
        .password = DESK_CLOCK_WIFI_THIRD_PASSWORD,
    },
};

static const size_t s_network_count = sizeof(s_networks) / sizeof(s_networks[0]);

static esp_netif_t *s_sta_netif;
static esp_timer_handle_t s_reconnect_timer;
static std::atomic<wifi_manager_state_t> s_state{WIFI_MANAGER_IDLE};
static std::atomic<bool> s_connect_requested{false};
static int s_retry_count;
static size_t s_network_index;
static size_t s_network_attempt_count;
static portMUX_TYPE s_ip_lock = portMUX_INITIALIZER_UNLOCKED;
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

static const char *network_role(size_t index)
{
    return index == 0 ? "primary" : (index == 1 ? "backup" : "third");
}

static esp_err_t apply_network_config(size_t index)
{
    if (index >= s_network_count) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_config_t config = {};
    strlcpy((char *)config.sta.ssid, s_networks[index].ssid,
            sizeof(config.sta.ssid));
    strlcpy((char *)config.sta.password, s_networks[index].password,
            sizeof(config.sta.password));
    config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;

    return esp_wifi_set_config(WIFI_IF_STA, &config);
}

static bool switch_to_next_network(void)
{
    if (s_network_count < 2) {
        return false;
    }

    s_network_index = (s_network_index + 1) % s_network_count;
    s_network_attempt_count = 0;
    s_retry_count = 0;

    const esp_err_t err = apply_network_config(s_network_index);
    if (err != ESP_OK) {
        set_state(WIFI_MANAGER_ERROR);
        ESP_LOGE(TAG, "Unable to select %s Wi-Fi '%s': %s",
                 network_role(s_network_index), s_networks[s_network_index].ssid,
                 esp_err_to_name(err));
        return false;
    }

    ESP_LOGW(TAG, "Switching to %s Wi-Fi '%s'",
             network_role(s_network_index), s_networks[s_network_index].ssid);
    return true;
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
        s_network_attempt_count = s_network_attempt_count + 1;
        set_state(WIFI_MANAGER_CONNECTING);
        ESP_LOGW(TAG, "Disconnected from '%s' (reason %u)",
                 s_networks[s_network_index].ssid, event->reason);
        if (s_network_attempt_count >= CONNECT_FAILOVER_THRESHOLD &&
            !switch_to_next_network()) {
            return;
        }
        schedule_reconnect();
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
        char ip[16];
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&event->ip_info.ip));
        portENTER_CRITICAL(&s_ip_lock);
        memcpy(s_ip, ip, sizeof(s_ip));
        portEXIT_CRITICAL(&s_ip_lock);
        s_retry_count = 0;
        s_network_attempt_count = 0;
        esp_timer_stop(s_reconnect_timer);
        set_state(WIFI_MANAGER_CONNECTED);
        ESP_LOGI(TAG, "Connected to %s Wi-Fi '%s', IP: %s",
                 network_role(s_network_index), s_networks[s_network_index].ssid,
                 ip);
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
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) return err;
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

    s_network_index = 0;
    s_network_attempt_count = 0;
    ESP_LOGI(TAG, "Connecting to %s Wi-Fi '%s'", network_role(s_network_index),
             s_networks[s_network_index].ssid);
    s_connect_requested = true;
    set_state(WIFI_MANAGER_CONNECTING);

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) {
        err = apply_network_config(s_network_index);
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
    portENTER_CRITICAL(&s_ip_lock);
    strlcpy(out, s_ip, size);
    portEXIT_CRITICAL(&s_ip_lock);
}
