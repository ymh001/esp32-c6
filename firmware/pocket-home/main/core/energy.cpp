#include "energy.h"
#include "file_transfer.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "services.h"
#include <cstring>
#include <new>
#if __has_include("../../config.local.h")
#include "../../config.local.h"
#endif
#ifndef POCKET_GRAFANA_URL
#define POCKET_GRAFANA_URL ""
#define POCKET_GRAFANA_TOKEN ""
#endif
namespace pocket {
namespace {
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
EnergySnapshot published;
constexpr char query[] =
    R"({"queries":[{"refId":"A","datasource":{"uid":"home-history","type":"grafana-postgresql-datasource"},"format":"table","rawSql":"SELECT (SELECT kwh FROM energy_totals WHERE period='今日') AS kwh,(SELECT cost FROM energy_totals WHERE period='今日') AS cost,remaining_kwh*1.1 AS balance,extract(epoch FROM updated_at) AS updated FROM meter_summary LIMIT 1"}],"from":"now-1h","to":"now"})";
struct Response {
  char body[6144];
  size_t used = 0;
  bool overflow = false;
};
esp_err_t event(esp_http_client_event_t *e) {
  auto r = static_cast<Response *>(e->user_data);
  if (e->event_id == HTTP_EVENT_ON_DATA && e->data_len > 0) {
    if (r->used + e->data_len >= sizeof(r->body)) {
      r->overflow = true;
      return ESP_FAIL;
    }
    memcpy(r->body + r->used, e->data, e->data_len);
    r->used += e->data_len;
    r->body[r->used] = 0;
  }
  return ESP_OK;
}
bool fetch(EnergySnapshot &next) {
  auto response = new (std::nothrow) Response{};
  if (!response)
    return false;
  esp_http_client_config_t cfg{};
  cfg.url = POCKET_GRAFANA_URL;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.timeout_ms = 12000;
  cfg.event_handler = event;
  cfg.user_data = response;
  cfg.disable_auto_redirect = true;
  auto client = esp_http_client_init(&cfg);
  bool ok = false;
  if (client) {
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Authorization",
                               "Bearer " POCKET_GRAFANA_TOKEN);
    esp_http_client_set_post_field(client, query, sizeof(query) - 1);
    auto err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    ok = err == ESP_OK && status == 200 && !response->overflow &&
         energy_parse(response->body, response->used + 1, next);
    if (!ok)
      ESP_LOGW("energy", "Fetch failed: %s HTTP %d", esp_err_to_name(err),
               status);
    esp_http_client_cleanup(client);
  }
  delete response;
  return ok;
}
void worker(void *) {
  unsigned remaining = 0;
  for (;;) {
    bool connected = network_snapshot().state == NetState::Connected;
    if (!connected)
      remaining = 0;
    if (connected && clock_valid() && remaining == 0 && !transfer_busy()) {
      EnergySnapshot next;
      bool ok = fetch(next);
      portENTER_CRITICAL(&lock);
      if (ok) {
        next.fetched = time(nullptr);
        published = next;
      } else
        published.failed = true;
      portEXIT_CRITICAL(&lock);
      if (ok)
        ESP_LOGI("energy",
                 "Updated kwh=%.2f cost=%.2f balance=%.2f source=%lld",
                 next.kwh, next.cost, next.balance, (long long)next.updated);
      remaining = 60;
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    if (remaining)
      --remaining;
  }
}
} // namespace
EnergySnapshot energy_snapshot() {
  portENTER_CRITICAL(&lock);
  auto result = published;
  portEXIT_CRITICAL(&lock);
  return result;
}
void energy_init() {
  if (!POCKET_GRAFANA_URL[0] || !POCKET_GRAFANA_TOKEN[0])
    return;
  if (xTaskCreate(worker, "energy", 8192, nullptr, 3, nullptr) != pdPASS)
    ESP_LOGE("energy", "Unable to create worker");
}
} // namespace pocket
