#include "device_power.h"
#include <string.h>

device_power_t device_power_parse(const char *state,bool climate)
{
    if(!state || !strcmp(state,"unknown") || !strcmp(state,"unavailable"))return DEVICE_UNAVAILABLE;
    if(!strcmp(state,"off"))return DEVICE_OFF;
    if(!climate)return !strcmp(state,"on") ? DEVICE_ON : DEVICE_UNAVAILABLE;
    const char *modes[]={"cool","heat","auto","heat_cool","fan_only","dry"};
    for(const char *mode:modes)
        if(!strcmp(state,mode))return DEVICE_ON;
    return DEVICE_UNAVAILABLE;
}
