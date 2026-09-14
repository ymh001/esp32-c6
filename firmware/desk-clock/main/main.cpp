#include <stdbool.h>

#include "board.h"
#include "clock_settings.h"
#include "clock_ui.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "time_service.h"
#include "wifi_manager.h"

static const char *TAG = "desk-clock";

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(clock_settings_init());

    clock_settings_t settings;
    ESP_ERROR_CHECK(clock_settings_load(&settings));

    const esp_err_t board_error = board_init();
    if (board_error != ESP_OK) {
        ESP_LOGE(TAG, "Board initialization failed: %s",
                 esp_err_to_name(board_error));
        return;
    }
    ESP_ERROR_CHECK(time_service_init(&settings));
    ESP_ERROR_CHECK(wifi_manager_start());
    ESP_ERROR_CHECK(clock_ui_start(&settings));

    bool key_was_pressed = false;
    bool time_sync_started = false;

    ESP_LOGI(TAG, "Desk clock started");

    while (true) {
        if (!time_sync_started && wifi_manager_is_connected()) {
            const esp_err_t time_error = time_service_start();
            if (time_error == ESP_OK) {
                time_sync_started = true;
            } else {
                ESP_LOGW(TAG, "Unable to start time synchronization: %s",
                         esp_err_to_name(time_error));
            }
        }

        const bool key_pressed = board_key_pressed();
        if (key_pressed && !key_was_pressed) {
            clock_ui_toggle_view();
        }
        key_was_pressed = key_pressed;

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
