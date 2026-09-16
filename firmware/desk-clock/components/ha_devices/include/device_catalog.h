#pragma once
#include <stddef.h>
struct device_definition_t { const char *name,*entity,*domain; };
// Add entries here: worker queues, state arrays and the scrolling UI size together.
static constexpr device_definition_t DEVICE_CATALOG[]={
    {"客厅灯","light.lemesh_wy02_d081_light","light"},
    {"卧室灯","light.lemesh_wy02_6e67_light","light"},
    {"屏幕挂灯","light.yeelink_lamp22_52c4_light","light"},
    {"风扇","fan.zhimi_fa1_3192_fan","fan"},
    {"空调","climate.cuco_cp6_56e3_air_conditioner","climate"},
    {"打印机","switch.cuco_v3_6c00_switch","switch"},
};
static constexpr size_t HA_DEVICE_COUNT=sizeof(DEVICE_CATALOG)/sizeof(DEVICE_CATALOG[0]);
