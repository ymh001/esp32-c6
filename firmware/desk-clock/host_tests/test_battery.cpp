#include "battery_math.h"
#include <assert.h>
#include <stdio.h>
int main()
{
    assert(battery_display_percent(4193, 0x28, 0x33) == 99); // CV, not finished
    assert(battery_display_percent(4193, 0x28, 0x14) == 100); // PMIC done
    assert(battery_display_percent(4200, 0x28, 0x33) == 99); // voltage not proof
    assert(battery_display_percent(4193, 0x08, 0x14) == 99); // stale done, unplugged
    assert(battery_display_percent(4193, 0x28, 0x54) == 99); // discharging
    assert(battery_display_percent(4193, 0x20, 0x14) == 0); // battery absent
    assert(battery_display_percent(3800, 0x08, 0x55) == 50);
    assert(battery_display_percent(2999, 0x08, 0x55) == 0);
    for (unsigned v = 0; v < 6000; ++v)
        assert(battery_display_percent(v, 0x08, 0x55) <= 100);
    puts("battery tests passed");
}
