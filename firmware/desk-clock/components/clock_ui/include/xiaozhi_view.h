#pragma once
#include "lvgl.h"
lv_obj_t *xiaozhi_view_create(lv_event_cb_t navigation, lv_event_cb_t speak = nullptr);
void xiaozhi_view_update(int state, const char *message);
