#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "clock_settings.h"
#include "esp_err.h"

typedef enum {
    NET_STATE_IDLE = 0,
    NET_STATE_CONNECTING,
    NET_STATE_CONNECTED,
    NET_STATE_PORTAL,
    NET_STATE_ERROR,
} net_state_t;

esp_err_t net_config_start(const clock_settings_t *settings);
net_state_t net_config_state(void);
bool net_config_is_connected(void);
bool net_config_is_portal(void);
void net_config_get_ip(char *out, size_t size);
void net_config_request_portal(void);
