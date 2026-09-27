#include "board.h"
#include "core/diagnostics.h"
#include "core/services.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ui/port.h"
#include "ui/shell.h"
extern "C" void app_main() {
  const esp_pm_config_t pm{.max_freq_mhz=160,.min_freq_mhz=40,.light_sleep_enable=false};
  ESP_ERROR_CHECK(esp_pm_configure(&pm));
  pocket::diagnostics_init();
  ESP_ERROR_CHECK(board_init());
  pocket::services_init();
  display_port_init();
  pocket::ui_start();
  bool held = false;
  uint32_t heartbeat = 0;
  ESP_LOGI("pocket-home",
           "v0.2.3 ready (SD read/write): desktop, clock, energy, control center, Wi-Fi");
  for (;;) {
    bool key = board_key_pressed();
    if (key && !held)
      pocket::ui_home();
    held = key;
    pocket::services_process();
    display_port_process();
    pocket::diagnostics_process();
    uint32_t wait = lv_timer_handler();
    if (lv_tick_elaps(heartbeat) > 30000) {
      heartbeat = lv_tick_get();
      ESP_LOGI("health", "heap=%u largest=%u stack=%u",
               (unsigned)esp_get_free_heap_size(),
               (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
               (unsigned)uxTaskGetStackHighWaterMark(nullptr));
    }
    vTaskDelay(pdMS_TO_TICKS(wait < 5 ? 5 : wait > 20 ? 20 : wait));
  }
}
