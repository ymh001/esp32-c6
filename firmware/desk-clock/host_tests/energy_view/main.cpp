#include "energy_view.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static uint16_t pixels[480 * 480];
static uint16_t strip[480 * 32];
static unsigned clicks;
static void clicked(lv_event_t *) { ++clicks; }
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *data)
{
    const auto *source = reinterpret_cast<uint16_t *>(data);
    for (int y = area->y1; y <= area->y2; ++y)
        for (int x = area->x1; x <= area->x2; ++x)
            pixels[y * 480 + x] = *source++;
    lv_display_flush_ready(display);
}
static void dump(const char *path)
{
    FILE *file = fopen(path, "wb");
    assert(file);
    fprintf(file, "P6\n480 480\n255\n");
    for (uint16_t pixel : pixels) {
        const unsigned char rgb[] = {
            (unsigned char)(((pixel >> 11) & 31) * 255 / 31),
            (unsigned char)(((pixel >> 5) & 63) * 255 / 63),
            (unsigned char)((pixel & 31) * 255 / 31),
        };
        fwrite(rgb, 1, 3, file);
    }
    fclose(file);
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    setenv("TZ", "CST-8", 1);
    tzset();
    lv_init();
    lv_display_t *display = lv_display_create(480, 480);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, strip, NULL, sizeof(strip), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    energy_view_t view = energy_view_create(clicked);
    lv_screen_load(view.screen);
    energy_view_data_t data = {};
    energy_view_update(&view, &data);
    assert(strcmp(lv_label_get_text(view.values[0]), "--") == 0);
    data.loaded = data.month_loaded = true;
    data.today = 5.25f; data.week = 32.75f; data.month = 75.51f; data.remaining = 121.84f;
    struct tm updated = {};
    updated.tm_year = 126; updated.tm_mon = 8; updated.tm_mday = 15;
    updated.tm_hour = 21; updated.tm_min = 30;
    data.updated_at = mktime(&updated);
    energy_view_update(&view, &data);
    lv_refr_now(display);
    dump(argv[1]);
    assert(lv_obj_get_width(view.refresh) == 432 && lv_obj_get_height(view.refresh) == 70);
    assert(strcmp(lv_label_get_text(view.status), "● 已更新") == 0);
    assert(strcmp(lv_label_get_text(view.updated), "更新于 21:30") == 0);
    lv_obj_send_event(view.refresh, LV_EVENT_CLICKED, NULL);
    assert(clicks == 1);
    data.refreshing = true;
    energy_view_update(&view, &data);
    assert(lv_obj_has_state(view.refresh, LV_STATE_DISABLED));
    data.refreshing = false; data.failed = true;
    energy_view_update(&view, &data);
    assert(!lv_obj_has_state(view.refresh, LV_STATE_DISABLED));
    assert(strcmp(lv_label_get_text(view.status), "● 更新失败") == 0);
    assert(strcmp(lv_label_get_text(view.values[0]), "5.25") == 0);
    data.failed = false; data.partial = true;
    energy_view_update(&view, &data);
    assert(strcmp(lv_label_get_text(view.status), "● 部分更新") == 0);
    data.remaining = 123456.78f; data.today = NAN;
    energy_view_update(&view, &data);
    assert(strcmp(lv_label_get_text(view.values[0]), "--") == 0);
    lv_refr_now(display);
    assert(lv_mem_test() == LV_RESULT_OK);
    puts("energy view tests passed");
}
