#include <stdbool.h>

#include "board.h"
#include "clock_settings.h"
#include "clock_ui.h"
#include "energy_service.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "time_service.h"
#include "wifi_manager.h"
#include "esp_pm.h"
#include "network_gate.h"
#include "ha_devices.h"

static const char *TAG = "desk-clock";

extern "C" void app_main(void)
{
    const esp_pm_config_t pm = {.max_freq_mhz=160, .min_freq_mhz=40, .light_sleep_enable=false};
    ESP_ERROR_CHECK(esp_pm_configure(&pm));
    ESP_LOGI(TAG, "CPU dynamic frequency: 40–160 MHz, light sleep disabled");
    ESP_ERROR_CHECK(clock_settings_init());

    clock_settings_t settings;
    ESP_ERROR_CHECK(clock_settings_load(&settings));
    clock_settings_request_save(&settings);
    ESP_LOGI(TAG,"Settings: 24-hour clock, sync=%u min, screen off=%u sec, plugged awake=%u",
             settings.sync_minutes,settings.screen_off_seconds,settings.stay_awake_on_power);

    const esp_err_t board_error = board_init();
    if (board_error != ESP_OK) {
        ESP_LOGE(TAG, "Board initialization failed: %s",
                 esp_err_to_name(board_error));
        return;
    }
    ESP_ERROR_CHECK(time_service_init(&settings));
    ESP_ERROR_CHECK(wifi_manager_start());
    ESP_ERROR_CHECK(network_gate_init());
    network_set_sync_minutes(settings.sync_minutes);
    ESP_ERROR_CHECK(energy_service_init());

    ESP_ERROR_CHECK(ha_devices_init());
    ESP_ERROR_CHECK(clock_ui_start(&settings));

    bool key_was_pressed = false;
    bool time_sync_started = false;
    bool energy_started = false;
    TickType_t last_rotation_poll = 0;

    ESP_LOGI(TAG, "Desk clock started");

    while (true) {
        clock_settings_process();
        time_service_process();
        if(network_process()){
            ha_devices_refresh();
            if(energy_started)energy_service_request_refresh();
        }
        clock_ui_poll_battery();
        if (!time_sync_started && wifi_manager_is_connected()) {
            const esp_err_t time_error = time_service_start();
            if (time_error == ESP_OK) {
                time_sync_started = true;
            } else {
                ESP_LOGW(TAG, "Unable to start time synchronization: %s",
                         esp_err_to_name(time_error));
            }
        }

        if (!energy_started && wifi_manager_is_connected() &&
            (time_service_is_synced() || time(nullptr)>1700000000)) {
            const esp_err_t energy_error = energy_service_start();
            if (energy_error == ESP_OK) {
                energy_started = true;
            } else {
                ESP_LOGW(TAG, "Unable to start energy service: %s",
                         esp_err_to_name(energy_error));
            }
        }

        const bool key_pressed = board_key_pressed();
        if (key_pressed && !key_was_pressed) {
            clock_ui_toggle_view();
        }
        key_was_pressed = key_pressed;

        const TickType_t now = xTaskGetTickCount();
        if (now - last_rotation_poll >= pdMS_TO_TICKS(100)) {
            last_rotation_poll = now;
            clock_ui_auto_rotate_update();
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
