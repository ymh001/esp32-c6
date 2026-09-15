#pragma once
#include "lvgl.h"
#include "device_power.h"

struct device_card_data_t { device_power_t power; bool busy, error; };
struct devices_view_data_t { device_card_data_t cards[4]; bool refreshing; const char *message; };
struct devices_view_t {
    lv_obj_t *screen,*status,*refresh,*refresh_label;
    lv_obj_t *cards[4],*power[4],*action[4],*dots[4];
};
devices_view_t devices_view_create(lv_event_cb_t navigate,lv_event_cb_t toggle,lv_event_cb_t refresh);
void devices_view_update(devices_view_t *view,const devices_view_data_t *data);
