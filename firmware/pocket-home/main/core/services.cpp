#include "services.h"
#include "file_transfer.h"
#include "board.h"
#include "energy.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <atomic>
#include <cstdlib>
#include <sys/time.h>
namespace pocket {
void network_init();
static Preferences prefs;
static BatterySnapshot battery;
static int64_t battery_next;
BatterySnapshot battery_snapshot() { return battery; }
static int64_t save_at;
static bool ntp_started;
static std::atomic<bool> synced{false}, rtc_pending{false};
static const char *TAG = "services";
void services_init() {
  ESP_ERROR_CHECK(
      nvs_flash_init()); // Never erase the user's settings on init failure.
  nvs_handle_t h;
  if (nvs_open("pocket_ui", NVS_READONLY, &h) == ESP_OK) {
    nvs_get_u8(h, "brightness", &prefs.brightness);
    nvs_get_u16(h, "timeout", &prefs.timeout_seconds);
    uint8_t locked = 0;
    nvs_get_u8(h, "rot_lock", &locked);
    prefs.rotation_locked = locked == 1;
    nvs_get_u8(h, "rotation", &prefs.locked_rotation);
    if (prefs.locked_rotation > 3)
      prefs.locked_rotation = 0;
    nvs_close(h);
  }
  if (prefs.brightness < 5 || prefs.brightness > 100)
    prefs.brightness = 75;
  if (prefs.timeout_seconds != 0 && prefs.timeout_seconds != 30 &&
      prefs.timeout_seconds != 60 && prefs.timeout_seconds != 120 &&
      prefs.timeout_seconds != 300)
    prefs.timeout_seconds = 0;
  setenv("TZ", "CST-8", 1);
  tzset();
  tm t{};
  if (board_rtc_read(&t) == ESP_OK && t.tm_year >= 124 && t.tm_year <= 199) {
    timeval tv{.tv_sec = mktime(&t), .tv_usec = 0};
    settimeofday(&tv, nullptr);
  }
  network_init();
  energy_init();
}
Preferences preferences() { return prefs; }
void preferences_set(Preferences v) {
  prefs = v;
  save_at = esp_timer_get_time() + 800000;
}
void display_brightness(uint8_t v) { board_set_backlight(v); }
void clock_local(tm *out) {
  time_t t = time(nullptr);
  localtime_r(&t, out);
}
bool clock_valid() {
  tm t{};
  clock_local(&t);
  return t.tm_year >= 124;
}
bool clock_synced() { return synced; }
void services_process() {
  transfer_process();
  if (esp_timer_get_time() >= battery_next) {
    battery_next = esp_timer_get_time() + 5000000;
    battery.valid = board_power_get_battery(&battery.percent, &battery.voltage_mv) == ESP_OK;
  }
  if (save_at && esp_timer_get_time() >= save_at) {
    nvs_handle_t h;
    esp_err_t e = nvs_open("pocket_ui", NVS_READWRITE, &h);
    if (e == ESP_OK) {
      e = nvs_set_u8(h, "brightness", prefs.brightness);
      if (e == ESP_OK)
        e = nvs_set_u16(h, "timeout", prefs.timeout_seconds);
      if (e == ESP_OK)
        e = nvs_set_u8(h, "rot_lock", prefs.rotation_locked);
      if (e == ESP_OK)
        e = nvs_set_u8(h, "rotation", prefs.locked_rotation);
      if (e == ESP_OK)
        e = nvs_commit(h);
      nvs_close(h);
    }
    if (e != ESP_OK)
      ESP_LOGW(TAG, "Settings save failed: %s", esp_err_to_name(e));
    save_at = 0;
  }
  if (!ntp_started && network_snapshot().state == NetState::Connected) {
    esp_sntp_config_t c = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        2, ESP_SNTP_SERVER_LIST("ntp.aliyun.com", "pool.ntp.org"));
    c.sync_cb = [](timeval *) {
      synced = true;
      rtc_pending = true;
      ESP_LOGI(TAG, "Time synchronized");
    };
    ntp_started = esp_netif_sntp_init(&c) == ESP_OK;
  }
  if (rtc_pending.exchange(false)) {
    tm t{};
    clock_local(&t);
    board_rtc_write(&t);
  }
}
} // namespace pocket
