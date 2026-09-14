#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef struct {
    bool loaded;
    bool month_loaded;
    bool refreshing;
    float today_kwh;
    float week_kwh;
    float month_kwh;
    float remaining_kwh;
    char message[64];
} energy_snapshot_t;

esp_err_t energy_service_start(void);
void energy_service_request_refresh(void);
void energy_service_get_snapshot(energy_snapshot_t *snapshot);
