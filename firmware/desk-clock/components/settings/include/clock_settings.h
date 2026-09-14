#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define CLOCK_WIFI_SSID_MAX 33
#define CLOCK_WIFI_PASSWORD_MAX 65
#define CLOCK_TIMEZONE_MAX 96
#define CLOCK_NTP_SERVER_MAX 64

typedef struct {
    char wifi_ssid[CLOCK_WIFI_SSID_MAX];
    char wifi_password[CLOCK_WIFI_PASSWORD_MAX];
    char timezone[CLOCK_TIMEZONE_MAX];
    char ntp_server_1[CLOCK_NTP_SERVER_MAX];
    char ntp_server_2[CLOCK_NTP_SERVER_MAX];
    uint8_t use_24_hour;
    uint8_t brightness;
    uint8_t night_brightness;
    uint8_t night_start_hour;
    uint8_t night_end_hour;
    bool force_portal;
} clock_settings_t;

esp_err_t clock_settings_init(void);
void clock_settings_defaults(clock_settings_t *settings);
esp_err_t clock_settings_load(clock_settings_t *settings);
esp_err_t clock_settings_save(const clock_settings_t *settings);
esp_err_t clock_settings_set_portal_request(bool enabled);
