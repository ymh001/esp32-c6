#pragma once
#include "lvgl.h"
#include <stdint.h>

LV_FONT_DECLARE(clock_cjk_16);

// Every main page reserves y=428..479 for the same three navigation targets.
inline lv_obj_t *main_navigation_create(lv_obj_t *screen, unsigned selected,
                                        lv_event_cb_t navigate)
{
    auto bar=lv_obj_create(screen);lv_obj_remove_style_all(bar);
    lv_obj_set_pos(bar,0,428);lv_obj_set_size(bar,480,52);
    lv_obj_set_style_bg_color(bar,lv_color_black(),0);
    lv_obj_set_style_bg_opa(bar,LV_OPA_COVER,0);
    lv_obj_remove_flag(bar,LV_OBJ_FLAG_SCROLLABLE);
    const char *names[]={"时钟","设备","耗电"};
    for(unsigned i=0;i<3;++i){
        auto tab=lv_button_create(bar);lv_obj_remove_style_all(tab);
        lv_obj_set_pos(tab,i*160,0);lv_obj_set_size(tab,160,52);
        lv_obj_remove_flag(tab,LV_OBJ_FLAG_SCROLLABLE);
        if(navigate)lv_obj_add_event_cb(tab,navigate,LV_EVENT_CLICKED,(void *)(intptr_t)i);
        auto name=lv_label_create(tab);lv_label_set_text(name,names[i]);
        lv_obj_set_style_text_font(name,&clock_cjk_16,0);
        lv_obj_set_style_text_color(name,lv_color_hex(i==selected?0x80B9FF:0xD8DAE2),0);
        lv_obj_align(name,LV_ALIGN_TOP_MID,0,10);
    }
    auto mark=lv_obj_create(bar);lv_obj_remove_style_all(mark);
    lv_obj_set_pos(mark,selected*160+61,39);lv_obj_set_size(mark,38,3);
    lv_obj_set_style_bg_color(mark,lv_color_hex(0x80B9FF),0);
    lv_obj_set_style_bg_opa(mark,LV_OPA_COVER,0);
    lv_obj_remove_flag(mark,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(mark,LV_OBJ_FLAG_SCROLLABLE);
    return bar;
}
