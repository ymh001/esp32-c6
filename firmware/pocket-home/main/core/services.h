#pragma once
#include <cstdint>
#include <ctime>
namespace pocket {
struct Preferences {
  uint8_t brightness = 75;
  uint16_t timeout_seconds = 0;
  bool rotation_locked = false;
  uint8_t locked_rotation = 0;
};
enum class NetState { Off, Idle, Connecting, Connected, Failed };
struct AccessPoint {
  char ssid[33];
  int8_t rssi;
  bool secure;
  bool supported;
};
struct NetworkSnapshot {
  NetState state = NetState::Off;
  bool enabled = false, scanning = false;
  uint32_t revision = 0;
  char ssid[33]{}, saved_ssid[33]{}, ip[16]{}, message[96]{};
  uint8_t count = 0;
  AccessPoint aps[12]{};
};
struct BatterySnapshot {
  bool valid = false;
  uint8_t percent = 0;
  uint16_t voltage_mv = 0;
};
BatterySnapshot battery_snapshot();
void services_init();
void services_process();
Preferences preferences();
void preferences_set(Preferences value);
void display_brightness(uint8_t value);
NetworkSnapshot network_snapshot();
bool network_enable(bool enabled);
bool network_scan();
bool network_connect(const char *ssid, const char *password);
bool network_forget();
void clock_local(tm *out);
bool clock_valid();
bool clock_synced();
} // namespace pocket
