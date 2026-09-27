#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "services.h"
#include "file_transfer.h"
#include <cstdio>
#include <cstring>
#if __has_include("../../config.local.h")
#include "../../config.local.h"
#else
#define POCKET_WIFI_SSID ""
#define POCKET_WIFI_PASSWORD ""
#endif
namespace pocket {
namespace {
enum class Op { Enable, Disable, Scan, Connect, Forget };
struct Command {
  Op op;
  char ssid[33]{};
  char password[65]{};
};
struct Credentials {
  char ssid[33]{};
  char password[65]{};
};
QueueHandle_t commands;
EventGroupHandle_t events;
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
NetworkSnapshot published, current;
Credentials saved, candidate;
esp_netif_t *netif;
bool running = false, pending_save = false;
int64_t connect_at = 0, scan_at = 0, retry_at = 0;
constexpr EventBits_t SCAN_DONE = 1, LINK_UP = 2;
void publish() {
  ++current.revision;
  portENTER_CRITICAL(&lock);
  published = current;
  portEXIT_CRITICAL(&lock);
}
void message(const char *s) {
  snprintf(current.message, sizeof(current.message), "%s", s);
}
bool store_credentials() {
  nvs_handle_t h;
  if (nvs_open("pocket_net", NVS_READWRITE, &h) != ESP_OK)
    return false;
  esp_err_t e = nvs_set_blob(h, "network", &saved, sizeof(saved));
  if (e == ESP_OK)
    e = nvs_commit(h);
  nvs_close(h);
  return e == ESP_OK;
}
void store_enabled() {
  nvs_handle_t h;
  if (nvs_open("pocket_net", NVS_READWRITE, &h) == ESP_OK) {
    nvs_set_u8(h, "enabled", current.enabled);
    nvs_commit(h);
    nvs_close(h);
  }
}
void stop() {
  if (running) {
    if (current.scanning)
      esp_wifi_scan_stop();
    esp_wifi_stop();
  }
  running = false;
  current.scanning = false;
  connect_at = 0;
  scan_at = 0;
  retry_at = 0;
  current.ip[0] = 0;
  current.ssid[0] = 0;
  xEventGroupClearBits(events, SCAN_DONE | LINK_UP);
}
bool start() {
  if (running)
    return true;
  esp_err_t e = esp_wifi_start();
  if (e != ESP_OK) {
    current.state = NetState::Failed;
    message("Wi-Fi 启动失败，请重试");
    return false;
  }
  running = true;
  current.state = NetState::Idle;
  return true;
}
void connect_to(const Credentials &c, bool save) {
  stop();
  candidate = c;
  pending_save = save;
  wifi_config_t cfg{};
  memcpy(cfg.sta.ssid, c.ssid, strlen(c.ssid));
  memcpy(cfg.sta.password, c.password, strlen(c.password));
  cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
  cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
  cfg.sta.pmf_cfg.capable = true;
  cfg.sta.pmf_cfg.required = false;
  if (esp_wifi_set_config(WIFI_IF_STA, &cfg) != ESP_OK || !start() ||
      esp_wifi_connect() != ESP_OK) {
    current.state = NetState::Failed;
    message("无法连接，请重试");
    return;
  }
  snprintf(current.ssid, sizeof(current.ssid), "%s", c.ssid);
  current.state = NetState::Connecting;
  connect_at = esp_timer_get_time();
  message("正在连接…");
}
void scan() {
  if (current.scanning)
    return;
  if (current.state == NetState::Connecting) {
    stop();
    pending_save = false;
  }
  if (!start())
    return;
  xEventGroupClearBits(events, SCAN_DONE);
  wifi_scan_config_t cfg{};
  cfg.show_hidden = false;
  if (esp_wifi_scan_start(&cfg, false) != ESP_OK) {
    message("扫描失败，请重试");
    return;
  }
  current.scanning = true;
  scan_at = esp_timer_get_time();
  retry_at = 0;
  message("正在搜索附近网络…");
}
void collect_scan() {
  wifi_ap_record_t found[24]{};
  uint16_t n = 24;
  const esp_err_t e = esp_wifi_scan_get_ap_records(&n, found);
  current.scanning = false;
  scan_at = 0;
  current.count = 0;
  if (e != ESP_OK) {
    message("扫描失败，请重试");
    return;
  }
  for (unsigned i = 0; i < n && current.count < 12; i++) {
    if (!found[i].ssid[0])
      continue;
    bool duplicate = false;
    for (unsigned j = 0; j < current.count; j++)
      if (strncmp(current.aps[j].ssid, (char *)found[i].ssid, 32) == 0)
        duplicate = true;
    if (duplicate)
      continue;
    auto &ap = current.aps[current.count++];
    memcpy(ap.ssid, found[i].ssid, 32);
    ap.ssid[32] = 0;
    ap.rssi = found[i].rssi;
    ap.secure = found[i].authmode != WIFI_AUTH_OPEN;
    ap.supported = found[i].authmode == WIFI_AUTH_OPEN ||
                   found[i].authmode == WIFI_AUTH_WPA_PSK ||
                   found[i].authmode == WIFI_AUTH_WPA2_PSK ||
                   found[i].authmode == WIFI_AUTH_WPA_WPA2_PSK ||
                   found[i].authmode == WIFI_AUTH_WPA3_PSK ||
                   found[i].authmode == WIFI_AUTH_WPA2_WPA3_PSK;
  }
  ESP_LOGI("network", "Scan complete: %u visible networks", current.count);
  message(current.count ? "请选择网络" : "未找到网络，点击重新搜索");
  if (current.state != NetState::Connected && saved.ssid[0])
    retry_at = esp_timer_get_time() + 30000000;
}
void worker(void *) {
  nvs_handle_t h;
  uint8_t enabled = 1;
  bool loaded = false;
  if (nvs_open("pocket_net", NVS_READONLY, &h) == ESP_OK) {
    size_t n = sizeof(saved);
    loaded =
        nvs_get_blob(h, "network", &saved, &n) == ESP_OK && n == sizeof(saved);
    nvs_get_u8(h, "enabled", &enabled);
    nvs_close(h);
  }
  saved.ssid[32] = 0;
  saved.password[64] = 0;
  if (!loaded) {
    snprintf(saved.ssid, sizeof(saved.ssid), "%s", POCKET_WIFI_SSID);
    snprintf(saved.password, sizeof(saved.password), "%s",
             POCKET_WIFI_PASSWORD);
    store_credentials();
  }
  snprintf(current.saved_ssid, sizeof(current.saved_ssid), "%s", saved.ssid);
  current.enabled = enabled != 0;
  if (current.enabled) {
    if (saved.ssid[0])
      connect_to(saved, false);
    else {
      start();
      scan();
    }
  } else
    message("Wi-Fi 已关闭");
  publish();
  for (;;) {
    Command c{};
    if (xQueueReceive(commands, &c, pdMS_TO_TICKS(200)) == pdTRUE) {
      ESP_LOGI("network", "Request operation %u", (unsigned)c.op);
      switch (c.op) {
      case Op::Disable:
        stop();
        current.enabled = false;
        current.state = NetState::Off;
        current.count = 0;
        pending_save = false;
        message("Wi-Fi 已关闭");
        store_enabled();
        break;
      case Op::Enable:
        current.enabled = true;
        store_enabled();
        if (saved.ssid[0])
          connect_to(saved, false);
        else {
          start();
          scan();
        }
        break;
      case Op::Scan:
        if (current.enabled)
          scan();
        break;
      case Op::Connect:
        if (current.enabled) {
          Credentials next{};
          memcpy(next.ssid, c.ssid, sizeof(next.ssid));
          memcpy(next.password, c.password, sizeof(next.password));
          connect_to(next, true);
        }
        break;
      case Op::Forget:
        saved = {};
        pending_save = false;
        stop();
        current.saved_ssid[0] = 0;
        if (store_credentials()) {
          message("已忘记网络");
        } else
          message("保存失败，请重试");
        current.state = current.enabled ? NetState::Idle : NetState::Off;
        if (current.enabled) {
          start();
          scan();
        }
        break;
      }
      memset(c.password, 0, sizeof(c.password));
      publish();
    }
    if (!current.enabled || !running)
      continue;
    const int64_t now = esp_timer_get_time();
    if (current.scanning && (xEventGroupGetBits(events) & SCAN_DONE)) {
      xEventGroupClearBits(events, SCAN_DONE);
      collect_scan();
      publish();
    }
    if (current.scanning && now - scan_at > 15000000) {
      esp_wifi_scan_stop();
      esp_wifi_clear_ap_list();
      current.scanning = false;
      message("扫描超时，请重试");
      publish();
    }
    wifi_ap_record_t ap{};
    esp_netif_ip_info_t ip{};
    const bool online = (xEventGroupGetBits(events) & LINK_UP) &&
                        esp_wifi_sta_get_ap_info(&ap) == ESP_OK &&
                        esp_netif_is_netif_up(netif) &&
                        esp_netif_get_ip_info(netif, &ip) == ESP_OK &&
                        ip.ip.addr != 0;
    if (online && current.state != NetState::Connected) {
      current.state = NetState::Connected;
      connect_at = 0;
      retry_at = 0;
      snprintf(current.ip, sizeof(current.ip), IPSTR, IP2STR(&ip.ip));
      message("已连接");
      if (pending_save) {
        saved = candidate;
        if (!store_credentials())
          message("已连接，但保存失败");
        pending_save = false;
      }
      snprintf(current.saved_ssid, sizeof(current.saved_ssid), "%s",
               saved.ssid);
      ESP_LOGI("network", "Connected, IP %s", current.ip);
      publish();
    } else if (!online && current.state == NetState::Connected) {
      current.state = NetState::Idle;
      current.ip[0] = 0;
      message("连接断开，稍后重试");
      retry_at = now + 10000000;
      publish();
    } else if (current.state == NetState::Connecting && connect_at &&
               now - connect_at > 20000000) {
      const bool was_user = pending_save;
      stop();
      current.state = NetState::Failed;
      pending_save = false;
      message("连接失败，请检查密码和信号");
      if (!was_user && saved.ssid[0])
        retry_at = now + 30000000;
      publish();
    }
    if (retry_at && now >= retry_at && !current.scanning && saved.ssid[0]) {
      connect_to(saved, false);
      publish();
    }
  }
}
bool send(Command c) {
  return commands && xQueueSend(commands, &c, 0) == pdTRUE;
}
} // namespace
void network_init() {
  commands = xQueueCreate(8, sizeof(Command));
  events = xEventGroupCreate();
  assert(commands && events);
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  netif = esp_netif_create_default_wifi_sta();
  assert(netif);
  wifi_init_config_t c = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&c));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_event_handler_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID,
      [](void *, esp_event_base_t, int32_t id, void *) {
        if (id == WIFI_EVENT_SCAN_DONE)
          xEventGroupSetBits(events, SCAN_DONE);
        else if (id == WIFI_EVENT_STA_CONNECTED)
          xEventGroupSetBits(events, LINK_UP);
        else if (id == WIFI_EVENT_STA_DISCONNECTED || id == WIFI_EVENT_STA_STOP)
          xEventGroupClearBits(events, LINK_UP);
      },
      nullptr));
  BaseType_t ok = xTaskCreate(worker, "pocket_wifi", 6144, nullptr, 4, nullptr);
  assert(ok == pdPASS);
}
NetworkSnapshot network_snapshot() {
  portENTER_CRITICAL(&lock);
  auto copy = published;
  portEXIT_CRITICAL(&lock);
  return copy;
}
bool network_enable(bool on) { if(!on && transfer_enabled())return false; return send({on ? Op::Enable : Op::Disable}); }
bool network_scan() { return send({Op::Scan}); }
bool network_forget() { if(transfer_enabled())return false; return send({Op::Forget}); }
bool network_connect(const char *ssid, const char *password) {
  if(transfer_enabled())return false;
  if (!ssid || !password || !ssid[0] || strlen(ssid) > 32 ||
      strlen(password) > 63 || (password[0] && strlen(password) < 8))
    return false;
  Command c{Op::Connect};
  snprintf(c.ssid, sizeof(c.ssid), "%s", ssid);
  snprintf(c.password, sizeof(c.password), "%s", password);
  return send(c);
}
} // namespace pocket
