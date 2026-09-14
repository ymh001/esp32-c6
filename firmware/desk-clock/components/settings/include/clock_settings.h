#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define CLOCK_TIMEZONE_MAX 96
#define CLOCK_NTP_SERVER_MAX 64

typedef struct {
    char timezone[CLOCK_TIMEZONE_MAX];
    char ntp_server_1[CLOCK_NTP_SERVER_MAX];
    char ntp_server_2[CLOCK_NTP_SERVER_MAX];
    uint8_t use_24_hour;
    uint8_t brightness;
    uint8_t night_brightness;
    uint8_t night_start_hour;
    uint8_t night_end_hour;
} clock_settings_t;

esp_err_t clock_settings_init(void);
void clock_settings_defaults(clock_settings_t *settings);
esp_err_t clock_settings_load(clock_settings_t *settings);
esp_err_t clock_settings_save(const clock_settings_t *settings);
