#pragma once
#include "lvgl.h"
namespace pocket {
struct App {
  const char *id;
  const char *name;
  bool clock_icon;
  void (*create)(lv_obj_t *);
  void (*tick)();
  void (*close)();
};
const App *app_catalog(unsigned *count);
void flip_create(lv_obj_t *);
void flip_tick();
void flip_close();
void energy_create(lv_obj_t *);
void energy_tick();
void energy_close();
} // namespace pocket
