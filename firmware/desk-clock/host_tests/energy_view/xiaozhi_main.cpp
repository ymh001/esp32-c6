#include <time.h>

#include "xiaozhi_view.h"
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
    lv_obj_t *screen = xiaozhi_view_create(clicked);
    lv_screen_load(screen);
    lv_refr_now(display);
    dump(argv[1]);
    for (int state = 0; state <= 5; ++state) {
        xiaozhi_view_update(state, "客厅灯已经关闭。你还可以控制卧室灯、空调和窗帘。");
        lv_refr_now(display);
        assert(lv_mem_test() == LV_RESULT_OK);
    }
    assert(lv_mem_test() == LV_RESULT_OK);
    puts("xiaozhi view render passed");
}
