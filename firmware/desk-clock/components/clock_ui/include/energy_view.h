#pragma once

#include <time.h>
#include "lvgl.h"

struct energy_view_data_t {
    bool loaded;
    bool refreshing;
    bool failed;
    bool stale;
    float today;
    float today_cost;
    float remaining_cost;
    float price_per_kwh;
    time_t updated_at;
};

struct energy_view_t {
    lv_obj_t *screen;
    lv_obj_t *values[3]; // today kWh, today cost, remaining cost
    lv_obj_t *today_unit;
    lv_obj_t *price;
    lv_obj_t *status;
    lv_obj_t *updated;
    lv_obj_t *refresh;
    lv_obj_t *refresh_label;
    lv_obj_t *refresh_icon;
};

energy_view_t energy_view_create(lv_event_cb_t refresh_callback, lv_event_cb_t navigate = nullptr);
void energy_view_update(energy_view_t *view, const energy_view_data_t *data);
