#pragma once

#include <time.h>
#include "lvgl.h"

struct energy_view_data_t {
    bool loaded;
    bool month_loaded;
    bool refreshing;
    bool failed;
    bool partial;
    float today;
    float week;
    float month;
    float remaining;
    time_t updated_at;
};

struct energy_view_t {
    lv_obj_t *screen;
    lv_obj_t *values[4]; // today, week, month, remaining
    lv_obj_t *today_unit;
    lv_obj_t *status;
    lv_obj_t *updated;
    lv_obj_t *refresh;
    lv_obj_t *refresh_label;
    lv_obj_t *refresh_icon;
};

energy_view_t energy_view_create(lv_event_cb_t refresh_callback);
void energy_view_update(energy_view_t *view, const energy_view_data_t *data);
