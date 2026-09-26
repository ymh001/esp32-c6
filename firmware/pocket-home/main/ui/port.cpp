#include "port.h"
#include "../core/services.h"
#include "board.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"
#include "rotation.h"
#include "shell.h"
static SemaphoreHandle_t done;
static uint16_t *rotated;
static lv_indev_t *pointer;
static int64_t frame_started, wait_us;
static unsigned frame_pixels, frame_chunks;
static void refresh_metrics(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_REFR_START) {
    frame_started = esp_timer_get_time();
    wait_us = 0; frame_pixels = 0; frame_chunks = 0;
  } else if (frame_pixels >= 480 * 400) {
    ESP_LOGI("refresh", "page=%s rotation=%u total_us=%lld wait_us=%lld chunks=%u pixels=%u",
      pocket::ui_page(), (unsigned)lv_display_get_rotation(lv_display_get_default())*90,
      (long long)(esp_timer_get_time()-frame_started), (long long)wait_us, frame_chunks, frame_pixels);
  }
}
static bool transfer_done(esp_lcd_panel_io_handle_t,
                          esp_lcd_panel_io_event_data_t *, void *ctx) {
  BaseType_t wake = pdFALSE;
  lv_display_flush_ready((lv_display_t *)ctx);
  xSemaphoreGiveFromISR(done, &wake);
  return wake == pdTRUE;
}
static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *pixels) {
  frame_pixels += lv_area_get_size(a);
  ++frame_chunks;
  lv_area_t output = *a;
  auto rotation = lv_display_get_rotation(d);
  if (rotation != LV_DISPLAY_ROTATION_0) {
    assert(lv_area_get_size(a) <= 480 * 32);
    rotate_strip(reinterpret_cast<uint16_t *>(pixels), rotated,
                 lv_area_get_width(a), lv_area_get_height(a),
                 (unsigned)rotation);
    pixels = reinterpret_cast<uint8_t *>(rotated);
    lv_display_rotate_area(d, &output);
  }
  lv_draw_sw_rgb565_swap(pixels, lv_area_get_size(a));
  xSemaphoreTake(done, 0);
  ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(board_display()->panel, output.x1,
                                            output.y1, output.x2 + 1,
                                            output.y2 + 1, pixels));
}
static void wait_flush(lv_display_t *) {
  auto start = esp_timer_get_time();
  if (xSemaphoreTake(done, pdMS_TO_TICKS(2000)) != pdTRUE) {
    ESP_LOGE("display", "DMA timeout");
    abort();
  }
  wait_us += esp_timer_get_time() - start;
}
static void touch_read(lv_indev_t *, lv_indev_data_t *data) {
  static uint16_t last_x, last_y;
  uint16_t x, y, strength;
  uint8_t count = 0;
  auto touch = board_display()->touch;
  if (esp_lcd_touch_read_data(touch) == ESP_OK &&
      esp_lcd_touch_get_coordinates(touch, &x, &y, &strength, &count, 1) &&
      count) {
    last_x = x;
    last_y = y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else
    data->state = LV_INDEV_STATE_RELEASED;
  data->point.x = last_x;
  data->point.y = last_y;
}
void display_port_init() {
  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return esp_timer_get_time() / 1000; });
  auto d = lv_display_create(480, 480);
  assert(d);
  lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
  auto p = pocket::preferences();
  if (p.rotation_locked)
    lv_display_set_rotation(d, (lv_display_rotation_t)p.locked_rotation);
  void *buf =
      heap_caps_malloc(480 * 32 * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  assert(buf);
  void *next = heap_caps_malloc(480 * 32 * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  assert(next);
  rotated = (uint16_t *)heap_caps_malloc(480 * 32 * 2,
                                         MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  assert(rotated);
  lv_display_set_buffers(d, buf, next, 480 * 32 * 2,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  done = xSemaphoreCreateBinary();
  assert(done);
  esp_lcd_panel_io_callbacks_t cb{.on_color_trans_done = transfer_done};
  ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(
      board_display()->panel_io, &cb, d));
  lv_display_add_event_cb(d, refresh_metrics, LV_EVENT_REFR_START, nullptr);
  lv_display_add_event_cb(d, refresh_metrics, LV_EVENT_REFR_READY, nullptr);
  lv_display_set_flush_cb(d, flush);
  lv_display_set_flush_wait_cb(d, wait_flush);
  lv_display_add_event_cb(
      d,
      [](lv_event_t *e) {
        auto a = (lv_area_t *)lv_event_get_param(e);
        a->x1 &= ~1;
        a->y1 &= ~1;
        a->x2 |= 1;
        a->y2 |= 1;
      },
      LV_EVENT_INVALIDATE_AREA, nullptr);
  if (board_display()->touch) {
    auto i = lv_indev_create();
    pointer = i;
    lv_indev_set_type(i, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(i, touch_read);
    lv_indev_set_gesture_min_distance(i, 42);
    lv_indev_set_gesture_min_velocity(i, 4);
  }
}

void display_port_process() {
  static uint32_t last_poll;
  if (lv_tick_elaps(last_poll) < 100)
    return;
  last_poll = lv_tick_get();
  if (pocket::preferences().rotation_locked ||
      (pointer && lv_indev_get_state(pointer) == LV_INDEV_STATE_PRESSED))
    return;
  board_rotation_t pose;
  bool changed;
  if (board_auto_rotation_update(&pose, &changed) != ESP_OK)
    return;
  auto d = lv_display_get_default();
  auto desired = (lv_display_rotation_t)pose;
  if (lv_display_get_rotation(d) != desired) {
    lv_display_set_rotation(d, desired);
    lv_obj_invalidate(lv_screen_active());
    ESP_LOGI("display", "Orientation %s degrees", board_rotation_name(pose));
  }
}
