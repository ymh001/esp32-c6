#include <time.h>

#include "devices_view.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static uint16_t pixels[480 * 480];
static uint16_t strip[480 * 32];
static unsigned clicks;
static unsigned last_index;
static lv_indev_data_t pointer;
static void read_pointer(lv_indev_t *,lv_indev_data_t *data){*data=pointer;}
static void input(lv_indev_t *indev,int x,int y,bool down){
    pointer.point={x,y};pointer.state=down?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;
    lv_tick_inc(25);lv_indev_read(indev);lv_timer_handler();
}
static void clicked(lv_event_t *e) { ++clicks; last_index=(uintptr_t)lv_event_get_user_data(e); }
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
    assert(device_power_parse("cool",true)==DEVICE_ON);
    assert(device_power_parse("off",true)==DEVICE_OFF);
    assert(device_power_parse("on",false)==DEVICE_ON);
    assert(device_power_parse("unknown",false)==DEVICE_UNAVAILABLE);
    assert(device_power_parse("cool",false)==DEVICE_UNAVAILABLE);
    auto view=devices_view_create(clicked,clicked,clicked);
    lv_screen_load(view.screen);
    devices_view_data_t data={};data.message="上下滑动查看 · 点击开关控制";
    for(int i=0;i<HA_DEVICE_COUNT;++i)data.cards[i].power=i==3?DEVICE_ON:DEVICE_OFF;
    devices_view_update(&view,&data);
    lv_refr_now(display);dump(argv[1]);
    for(int i=0;i<HA_DEVICE_COUNT;++i){
        assert(!lv_obj_has_state(view.switches[i],LV_STATE_DISABLED));
        lv_obj_send_event(view.switches[i],LV_EVENT_PRESSED,nullptr);
        lv_obj_send_event(view.switches[i],LV_EVENT_CLICKED,nullptr);
        assert(last_index==(unsigned)i);
        data.cards[i].busy=true;devices_view_update(&view,&data);
        assert(lv_obj_has_state(view.switches[i],LV_STATE_DISABLED));
        data.cards[i].busy=false;data.cards[i].power=DEVICE_UNAVAILABLE;
        devices_view_update(&view,&data);
        assert(lv_obj_has_state(view.switches[i],LV_STATE_DISABLED));
        data.cards[i].power=DEVICE_OFF;data.cards[i].error=true;
        devices_view_update(&view,&data);
        assert(!lv_obj_has_state(view.switches[i],LV_STATE_DISABLED));
        assert(lv_mem_test()==LV_RESULT_OK);
    }
    assert(clicks==HA_DEVICE_COUNT);
    lv_obj_send_event(view.refresh,LV_EVENT_CLICKED,nullptr);assert(clicks==HA_DEVICE_COUNT+1);
    for(int i=0;i<HA_DEVICE_COUNT;++i){
        data.cards[i]={DEVICE_OFF,false,false};
        assert(!lv_obj_has_flag(view.cards[i],LV_OBJ_FLAG_CLICKABLE));
    }
    devices_view_update(&view,&data);
    auto indev=lv_indev_create();lv_indev_set_type(indev,LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev,read_pointer);lv_indev_set_mode(indev,LV_INDEV_MODE_EVENT);
    input(indev,100,140,true);input(indev,100,140,false);assert(clicks==HA_DEVICE_COUNT+1);
    input(indev,385,140,true);input(indev,355,140,true);input(indev,355,140,false);assert(clicks==HA_DEVICE_COUNT+1);
    input(indev,385,140,true);input(indev,385,185,true);input(indev,385,185,false);assert(clicks==HA_DEVICE_COUNT+1);
    input(indev,385,140,true);input(indev,385,140,false);assert(clicks==HA_DEVICE_COUNT+2 && last_index==0);
    auto extra=lv_obj_create(view.list);lv_obj_set_pos(extra,0,600);lv_obj_set_size(extra,400,68);
    lv_obj_update_layout(view.screen);lv_obj_scroll_to_y(view.list,300,LV_ANIM_OFF);
    assert(lv_obj_get_scroll_y(view.list)>0);
    assert(lv_obj_get_y(view.refresh)==16);
    assert(lv_obj_get_scroll_y(view.screen)==0);
    data.refreshing=true;devices_view_update(&view,&data);
    assert(lv_obj_has_state(view.refresh,LV_STATE_DISABLED));
    puts("devices view state, callbacks and render passed");
}
