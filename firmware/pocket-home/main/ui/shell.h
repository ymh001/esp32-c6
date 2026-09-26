#pragma once
#include "lvgl.h"
namespace pocket {
void ui_start();
void ui_home();
// Used by the native LVGL integration test, with the same navigation paths.
void ui_open_app(unsigned index);
void ui_control();
void ui_wifi();
const char *ui_page();
} // namespace pocket
