#pragma once

#include "clock_settings.h"
#include "esp_err.h"

esp_err_t clock_ui_start(const clock_settings_t *settings);
void clock_ui_toggle_view(void);
void clock_ui_show_clock(void);
void clock_ui_auto_rotate_update(void);

void clock_ui_poll_battery(void);
