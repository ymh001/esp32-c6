#include "main_navigation.h"
#include "devices_view.h"
#include <stdint.h>

LV_FONT_DECLARE(clock_cjk_16);
LV_FONT_DECLARE(clock_cjk_24);

static lv_obj_t *box(lv_obj_t *parent,int x,int y,int w,int h,uint32_t color,int radius)
{
    auto o=lv_obj_create(parent);lv_obj_remove_style_all(o);
    lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
    lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_set_style_radius(o,radius,0);lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);
    return o;
}
static lv_obj_t *label(lv_obj_t *parent,const char *text,const lv_font_t *font,int x,int y,uint32_t color)
{
    auto o=lv_label_create(parent);lv_label_set_text(o,text);lv_obj_set_pos(o,x,y);
    lv_obj_set_style_text_font(o,font,0);lv_obj_set_style_text_color(o,lv_color_hex(color),0);
    return o;
}
// A switch is a button drawn as a toggle. Its state changes only after HA confirms.
// Reject any drag, including horizontal drags which do not scroll the list.
static lv_event_cb_t toggle_callback;
static lv_point_t press_origin;
static bool dragged;
static void switch_event(lv_event_t *event)
{
    auto code=lv_event_get_code(event);
    auto input=(code==LV_EVENT_PRESSED || code==LV_EVENT_PRESSING)?lv_event_get_indev(event):nullptr;
    if(code==LV_EVENT_PRESSED){
        dragged=false;
        if(input)lv_indev_get_point(input,&press_origin);
    } else if(code==LV_EVENT_PRESSING && input){
        lv_point_t point;lv_indev_get_point(input,&point);
        if(LV_ABS(point.x-press_origin.x)>8 || LV_ABS(point.y-press_origin.y)>8)dragged=true;
    } else if(code==LV_EVENT_PRESS_LOST)dragged=true;
    else if(code==LV_EVENT_CLICKED && !dragged && toggle_callback){
        auto target=lv_event_get_target_obj(event);
        auto list=lv_obj_get_parent(lv_obj_get_parent(target));
        if(!lv_obj_is_scrolling(list) && !lv_obj_has_state(target,LV_STATE_DISABLED))toggle_callback(event);
    }
}
devices_view_t devices_view_create(lv_event_cb_t navigate,lv_event_cb_t toggle,lv_event_cb_t refresh)
{
    toggle_callback=toggle;
    devices_view_t view={};view.screen=lv_obj_create(nullptr);lv_obj_remove_style_all(view.screen);
    lv_obj_set_style_bg_color(view.screen,lv_color_black(),0);lv_obj_set_style_bg_opa(view.screen,LV_OPA_COVER,0);
    lv_obj_remove_flag(view.screen,LV_OBJ_FLAG_SCROLLABLE);
    label(view.screen,"设备控制",&clock_cjk_24,24,26,0xFFFFFF);
    view.status=label(view.screen,"上下滑动查看 · 点击开关控制",&clock_cjk_16,24,77,0xA2AAB8);
    lv_obj_set_width(view.status,432);lv_label_set_long_mode(view.status,LV_LABEL_LONG_DOT);
    view.refresh=box(view.screen,340,16,116,52,0x202937,12);
    lv_obj_add_event_cb(view.refresh,refresh,LV_EVENT_CLICKED,nullptr);
    view.refresh_label=label(view.refresh,"刷新",&clock_cjk_24,0,0,0xB8D4FF);lv_obj_center(view.refresh_label);
    view.list=box(view.screen,24,108,432,310,0,0);
    lv_obj_add_flag(view.list,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(view.list,LV_DIR_VER);
    lv_obj_set_scrollbar_mode(view.list,LV_SCROLLBAR_MODE_AUTO);
    lv_obj_remove_flag(view.list,LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_remove_flag(view.list,LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_remove_flag(view.list,LV_OBJ_FLAG_SCROLL_ELASTIC);
    for(size_t i=0;i<HA_DEVICE_COUNT;++i){
        auto card=view.cards[i]=box(view.list,0,i*78,432,68,0x161A20,12);
        lv_obj_remove_flag(card,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_border_width(card,1,0);lv_obj_set_style_border_color(card,lv_color_hex(0x30363D),0);
        // The 16px font contains all common CJK characters for future device names.
        auto name=label(card,DEVICE_CATALOG[i].name,&clock_cjk_16,16,7,0xFFFFFF);
        lv_obj_set_style_transform_pivot_x(name,0,0);lv_obj_set_style_transform_pivot_y(name,0,0);
        lv_obj_set_style_transform_scale(name,384,0);
        view.dots[i]=box(card,17,45,9,9,0x717987,5);lv_obj_remove_flag(view.dots[i],LV_OBJ_FLAG_CLICKABLE);
        view.power[i]=label(card,"读取中…",&clock_cjk_16,34,38,0xA2AAB8);
        auto button=view.switches[i]=box(card,326,14,86,40,0x454C56,20);
        lv_obj_add_event_cb(button,switch_event,LV_EVENT_ALL,(void *)(intptr_t)i);
        lv_obj_remove_flag(button,LV_OBJ_FLAG_GESTURE_BUBBLE);
        auto knob=view.knobs[i]=box(button,4,4,32,32,0xFFFFFF,16);
        lv_obj_remove_flag(knob,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_state(button,LV_STATE_DISABLED);
    }
    main_navigation_create(view.screen,1,navigate);
    return view;
}
void devices_view_update(devices_view_t *view,const devices_view_data_t *data)
{
    lv_label_set_text(view->status,data->message?data->message:"");
    lv_label_set_text(view->refresh_label,data->refreshing?"刷新中":"刷新");
    lv_obj_set_style_text_font(view->refresh_label,data->refreshing?&clock_cjk_16:&clock_cjk_24,0);
    if(data->refreshing)lv_obj_add_state(view->refresh,LV_STATE_DISABLED);
    else lv_obj_remove_state(view->refresh,LV_STATE_DISABLED);
    for(size_t i=0;i<HA_DEVICE_COUNT;++i){
        const auto &d=data->cards[i];bool on=d.power==DEVICE_ON;
        bool available=d.power==DEVICE_ON || d.power==DEVICE_OFF;
        uint32_t color=d.busy?0x8CBFFF:!available?0x8D96A4:on?0x55DEA0:0xD6DBE3;
        lv_obj_set_style_border_color(view->cards[i],lv_color_hex(d.error?0x986C3D:0x30363D),0);
        lv_obj_set_style_bg_color(view->dots[i],lv_color_hex(color),0);
        lv_obj_set_style_text_color(view->power[i],lv_color_hex(color),0);
        lv_label_set_text(view->power[i],d.busy?"处理中…":d.power==DEVICE_UNKNOWN?"读取中…":!available?"未连接":d.error?"操作失败，请重试":on?"已开启":"已关闭");
        lv_obj_set_style_bg_color(view->switches[i],lv_color_hex(on?0x19BD70:0x454C56),0);
        lv_obj_set_x(view->knobs[i],on?50:4);
        lv_obj_set_style_opa(view->switches[i],d.busy || !available?LV_OPA_50:LV_OPA_COVER,0);
        if(d.busy || !available)lv_obj_add_state(view->switches[i],LV_STATE_DISABLED);
        else lv_obj_remove_state(view->switches[i],LV_STATE_DISABLED);
    }
}
