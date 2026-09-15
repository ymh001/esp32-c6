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
devices_view_t devices_view_create(lv_event_cb_t navigate,lv_event_cb_t toggle,lv_event_cb_t refresh)
{
    devices_view_t view={};view.screen=lv_obj_create(nullptr);lv_obj_remove_style_all(view.screen);
    lv_obj_set_style_bg_color(view.screen,lv_color_black(),0);lv_obj_set_style_bg_opa(view.screen,LV_OPA_COVER,0);
    lv_obj_remove_flag(view.screen,LV_OBJ_FLAG_SCROLLABLE);
    label(view.screen,"设备",&clock_cjk_24,24,26,0xFFFFFF);
    view.status=label(view.screen,"正在读取设备状态…",&clock_cjk_16,24,77,0xA2AAB8);
    lv_obj_set_width(view.status,432);lv_label_set_long_mode(view.status,LV_LABEL_LONG_DOT);
    view.refresh=box(view.screen,340,16,116,52,0x202937,12);
    lv_obj_add_event_cb(view.refresh,refresh,LV_EVENT_CLICKED,nullptr);
    view.refresh_label=label(view.refresh,"刷新",&clock_cjk_24,0,0,0xB8D4FF);lv_obj_center(view.refresh_label);
    const char *names[]={"客厅灯","卧室灯","屏幕挂灯","空调"};
    for(int i=0;i<4;++i){
        auto card=view.cards[i]=box(view.screen,24+(i%2)*224,112+(i/2)*152,208,136,0x161A20,16);
        lv_obj_set_style_border_width(card,1,0);lv_obj_set_style_border_color(card,lv_color_hex(0x30363D),0);
        lv_obj_add_event_cb(card,toggle,LV_EVENT_CLICKED,(void *)(intptr_t)i);
        label(card,names[i],&clock_cjk_24,16,18,0xFFFFFF);
        view.dots[i]=box(card,176,27,12,12,0x717987,6);lv_obj_remove_flag(view.dots[i],LV_OBJ_FLAG_CLICKABLE);
        view.power[i]=label(card,"读取中…",&clock_cjk_24,16,57,0xA2AAB8);
        view.action[i]=label(card,"请稍候",&clock_cjk_16,16,105,0xA2AAB8);
        lv_obj_add_state(card,LV_STATE_DISABLED);
    }
    const char *tabs[]={"时钟","小智","设备","耗电"};
    for(int i=0;i<4;++i){
        auto tab=box(view.screen,i*120,428,120,52,0,0);
        lv_obj_add_event_cb(tab,navigate,LV_EVENT_CLICKED,(void *)(intptr_t)i);
        auto text=label(tab,tabs[i],&clock_cjk_16,0,0,i==2?0x80B9FF:0xD8DAE2);
        lv_obj_align(text,LV_ALIGN_TOP_MID,0,10);
    }
    auto underline=box(view.screen,281,467,38,3,0x80B9FF,1);lv_obj_remove_flag(underline,LV_OBJ_FLAG_CLICKABLE);
    return view;
}
void devices_view_update(devices_view_t *view,const devices_view_data_t *data)
{
    lv_label_set_text(view->status,data->message?data->message:"");
    lv_label_set_text(view->refresh_label,data->refreshing?"刷新中":"刷新");
    lv_obj_set_style_text_font(view->refresh_label,data->refreshing?&clock_cjk_16:&clock_cjk_24,0);
    if(data->refreshing)lv_obj_add_state(view->refresh,LV_STATE_DISABLED);
    else lv_obj_remove_state(view->refresh,LV_STATE_DISABLED);
    for(int i=0;i<4;++i){
        const auto &d=data->cards[i];bool on=d.power==DEVICE_ON;
        bool available=d.power==DEVICE_ON || d.power==DEVICE_OFF;
        uint32_t color=d.busy?0x8CBFFF:!available?0x8D96A4:on?0x55DEA0:0xD6DBE3;
        uint32_t border=d.error?0x986C3D:d.busy?0x3A608A:on?0x2A6448:0x30363D;
        lv_obj_set_style_bg_color(view->cards[i],lv_color_hex(on?0x12291F:0x161A20),0);
        lv_obj_set_style_border_color(view->cards[i],lv_color_hex(border),0);
        lv_obj_set_style_bg_color(view->dots[i],lv_color_hex(color),0);
        lv_obj_set_style_text_color(view->power[i],lv_color_hex(color),0);
        lv_label_set_text(view->power[i],d.busy?"处理中…":d.power==DEVICE_UNKNOWN?"读取中…":!available?"未连接":on?"已开启":"已关闭");
        lv_label_set_text(view->action[i],d.busy?"正在确认状态":d.power==DEVICE_UNKNOWN?"请稍候":!available?"点刷新重连":d.error?"操作失败 · 重试":on?"点击关闭":"点击开启");
        if(d.busy || !available)lv_obj_add_state(view->cards[i],LV_STATE_DISABLED);
        else lv_obj_remove_state(view->cards[i],LV_STATE_DISABLED);
    }
}
