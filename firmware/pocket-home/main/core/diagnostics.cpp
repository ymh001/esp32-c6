#include "diagnostics.h"
#include "../ui/shell.h"
#include "board.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "energy.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "services.h"
#include <cstring>
namespace pocket {
namespace {
char command[32];
unsigned length;
int test_step = -1;
uint32_t next_step;
Preferences original;
void status() {
  auto n = network_snapshot();
  auto b = battery_snapshot();
  ESP_LOGI("diagnostics", "battery_valid=%d percent=%u voltage_mv=%u", b.valid, b.percent, b.voltage_mv);
  auto e = energy_snapshot();
  ESP_LOGI("diagnostics", "minimum_heap=%u", (unsigned)esp_get_minimum_free_heap_size());
  ESP_LOGI("diagnostics",
           "energy_valid=%u failed=%d kwh=%.2f cost=%.2f balance=%.2f "
           "rotation=%d locked=%d",
           e.valid, e.failed, e.kwh, e.cost, e.balance,
           (int)lv_display_get_rotation(lv_display_get_default()) * 90,
           preferences().rotation_locked);
  lv_mem_monitor_t m;
  lv_mem_monitor(&m);
  ESP_LOGI("diagnostics",
           "page=%s touch=%s wifi=%d scanning=%d aps=%u ip=%s time=%s heap=%u "
           "lvgl_peak=%u "
           "lvgl_free=%u",
           ui_page(), board_display()->touch ? "ready" : "missing",
           (int)n.state, n.scanning, n.count, n.ip,
           clock_synced() ? "synced" : "local",
           (unsigned)esp_get_free_heap_size(), (unsigned)m.max_used,
           (unsigned)m.free_size);
}
void execute() {
  const char *known[] = {"home",     "clock",   "energy", "control", "wifi",
                         "wifi-off", "wifi-on", "status", "test"};
  bool recognized = false;
  for (const char *name : known)
    if (!strcmp(name, command))
      recognized = true;
  if (!recognized)
    return;
  ESP_LOGI("diagnostics", "Command: %s", command);
  if (!strcmp(command, "home"))
    ui_home();
  else if (!strcmp(command, "clock"))
    ui_open_app(0);
  else if (!strcmp(command, "energy"))
    ui_open_app(1);
  else if (!strcmp(command, "wifi-off"))
    network_enable(false);
  else if (!strcmp(command, "wifi-on"))
    network_enable(true);
  else if (!strcmp(command, "control"))
    ui_control();
  else if (!strcmp(command, "wifi")) {
    network_scan();
    ui_wifi();
  } else if (!strcmp(command, "status"))
    status();
  else if (!strcmp(command, "test") && test_step < 0) {
    original = preferences();
    auto p = original;
    p.timeout_seconds = 0;
    preferences_set(p);
    test_step = 0;
    next_step = lv_tick_get();
    ESP_LOGI("diagnostics", "SELFTEST START");
  }
}
} // namespace
void diagnostics_init() {
  usb_serial_jtag_driver_config_t cfg{.tx_buffer_size = 2048,
                                      .rx_buffer_size = 256};
  ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&cfg));
  usb_serial_jtag_vfs_use_driver();
}
void diagnostics_process() {
  char c;
  while (usb_serial_jtag_read_bytes(&c, 1, 0) == 1) {
    if (c == '\n' || c == '\r') {
      command[length] = 0;
      if (length)
        execute();
      length = 0;
    } else if (c >= 32 && c < 127 && length < sizeof(command) - 1)
      command[length++] = c;
  }
  if (test_step < 0 || (int32_t)(lv_tick_get() - next_step) < 0)
    return;
  assert(lv_mem_test() == LV_RESULT_OK);
  assert(heap_caps_check_integrity_all(true));
  if (test_step == 30) {
    preferences_set(original);
    lv_display_set_rotation(lv_display_get_default(), (lv_display_rotation_t)original.locked_rotation);
    ui_home();
    test_step = -1;
    status();
    ESP_LOGI("diagnostics",
             "SELFTEST PASS: 30 screen changes, four rotations and brightness writes");
    return;
  }
  switch (test_step % 5) {
  case 0:
    ui_home();
    break;
  case 1:
    ui_open_app(0);
    break;
  case 2:
    ui_open_app(1);
    break;
  case 3:
    ui_control();
    break;
  case 4:
    ui_wifi();
    break;
  }
  auto p = preferences();
  p.brightness = test_step % 2 ? 70 : 40;
  p.rotation_locked = true;
  lv_display_set_rotation(lv_display_get_default(), (lv_display_rotation_t)(test_step % 4));
  preferences_set(p);
  ++test_step;
  next_step = lv_tick_get() + 350;
}
} // namespace pocket
