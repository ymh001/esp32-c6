#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "clock_settings.h"
#include "esp_err.h"

esp_err_t time_service_init(const clock_settings_t *settings);
esp_err_t time_service_start(void);
void time_service_apply_timezone(const char *timezone);
bool time_service_is_synced(void);
int64_t time_service_last_sync_epoch(void);
void time_service_get_local(struct tm *out);

void time_service_process(void);
