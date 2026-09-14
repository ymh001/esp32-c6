#include <stdbool.h>

#include "board.h"
#include "clock_settings.h"
#include "clock_ui.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net_config.h"
#include "time_service.h"

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
    ESP_ERROR_CHECK(net_config_start(&settings));
    ESP_ERROR_CHECK(clock_ui_start(&settings));

    bool key_was_pressed = false;
    bool boot_was_pressed = false;
    TickType_t boot_pressed_at = 0;
    bool portal_requested = false;
    bool time_sync_started = false;

    ESP_LOGI(TAG, "Desk clock started");

    while (true) {
        if (!time_sync_started && net_config_is_connected()) {
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

        const bool boot_pressed = board_boot_pressed();
        if (boot_pressed && !boot_was_pressed) {
            boot_pressed_at = xTaskGetTickCount();
            portal_requested = false;
        } else if (boot_pressed && !portal_requested &&
                   xTaskGetTickCount() - boot_pressed_at >
                       pdMS_TO_TICKS(3000)) {
            portal_requested = true;
            ESP_LOGW(TAG, "Opening configuration portal");
            net_config_request_portal();
        }
        boot_was_pressed = boot_pressed;

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
