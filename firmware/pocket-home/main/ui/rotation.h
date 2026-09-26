#pragma once
#include <cstdint>
// Same counter-clockwise pixel mapping as LVGL's lv_display_rotate_area.
inline void rotate_strip(const uint16_t *src, uint16_t *dst, int w, int h,
                         unsigned rotation) {
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      int i = y * w + x;
      if (rotation == 1)
        i = (w - 1 - x) * h + y;
      else if (rotation == 2)
        i = (h - 1 - y) * w + w - 1 - x;
      else if (rotation == 3)
        i = x * h + h - 1 - y;
      dst[i] = src[y * w + x];
    }
  }
}
