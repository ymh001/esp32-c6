#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "device_power.h"

enum { HA_DEVICE_COUNT=4 };
struct ha_device_state_t { device_power_t power; bool busy, error; };
struct ha_devices_snapshot_t {
    ha_device_state_t devices[HA_DEVICE_COUNT];
    bool refreshing;
    char message[96];
    uint32_t revision;
};
esp_err_t ha_devices_init(void);
void ha_devices_set_visible(bool visible);
void ha_devices_refresh(void);
void ha_devices_toggle(unsigned index);
void ha_devices_snapshot(ha_devices_snapshot_t *out);
