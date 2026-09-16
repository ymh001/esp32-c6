#pragma once

#include <stdbool.h>
#include <time.h>

#include "esp_err.h"

typedef struct {
    bool loaded;
    bool refreshing;
    bool refresh_failed;
    bool stale;
    time_t updated_at;
    float today_kwh;
    float today_cost;
    float remaining_cost;
    float price_per_kwh;
    char message[64];
} energy_snapshot_t;

esp_err_t energy_service_init(void);
esp_err_t energy_service_start(void);
void energy_service_request_refresh(void);
void energy_service_get_snapshot(energy_snapshot_t *snapshot);
