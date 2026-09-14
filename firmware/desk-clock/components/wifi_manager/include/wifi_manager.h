#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

typedef enum {
    WIFI_MANAGER_IDLE = 0,
    WIFI_MANAGER_CONNECTING,
    WIFI_MANAGER_CONNECTED,
    WIFI_MANAGER_ERROR,
} wifi_manager_state_t;

esp_err_t wifi_manager_start(void);
wifi_manager_state_t wifi_manager_state(void);
bool wifi_manager_is_connected(void);
void wifi_manager_get_ip(char *out, size_t size);
