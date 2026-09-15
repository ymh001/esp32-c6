#include "battery_math.h"
#include <stddef.h>

static uint8_t battery_percent_from_voltage(uint16_t voltage_mv)
{
    static const uint16_t voltages[] = {
        3300, 3500, 3650, 3700, 3750, 3800, 3900, 4000, 4100, 4200,
    };
    static const uint8_t percents[] = {
        0, 10, 20, 30, 40, 50, 70, 80, 90, 100,
    };

    if (voltage_mv <= voltages[0]) {
        return 0;
    }
    for (size_t i = 1; i < sizeof(voltages) / sizeof(voltages[0]); ++i) {
        if (voltage_mv <= voltages[i]) {
            const uint16_t low_mv = voltages[i - 1];
            const uint16_t high_mv = voltages[i];
            const uint8_t low_percent = percents[i - 1];
            const uint8_t high_percent = percents[i];
            const uint32_t span = high_mv - low_mv;
            return (uint8_t)(
                low_percent +
                (uint32_t)(voltage_mv - low_mv) *
                    (high_percent - low_percent) / span);
        }
    }
    return 100;
}

uint8_t battery_display_percent(uint16_t voltage_mv, uint8_t status1, uint8_t status2)
{
    if ((status1 & 0x08) == 0) return 0;
    const bool external_power = (status1 & 0x20) != 0;
    const bool discharging = (status2 & 0x60) == 0x40;
    // REG01[2:0] = 4 means charge done. Voltage alone is not a full-charge signal.
    if (external_power && !discharging && (status2 & 0x07) == 4) return 100;
    uint8_t estimated = voltage_mv >= 3000 ? battery_percent_from_voltage(voltage_mv) : 0;
    if (external_power && estimated == 100) estimated = 99;
    return estimated;
}
