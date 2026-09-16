#pragma once
#include "lvgl.h"
#include "device_power.h"
#include "device_catalog.h"
struct device_card_data_t { device_power_t power; bool busy,error; };
struct devices_view_data_t { device_card_data_t cards[HA_DEVICE_COUNT]; bool refreshing; const char *message; };
struct devices_view_t {
    lv_obj_t *screen,*list,*status,*refresh,*refresh_label;
    lv_obj_t *cards[HA_DEVICE_COUNT],*power[HA_DEVICE_COUNT],*switches[HA_DEVICE_COUNT],*knobs[HA_DEVICE_COUNT],*dots[HA_DEVICE_COUNT];
};
devices_view_t devices_view_create(lv_event_cb_t navigate,lv_event_cb_t toggle,lv_event_cb_t refresh);
void devices_view_update(devices_view_t *view,const devices_view_data_t *data);
