#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "lunar.h"

static void expect_lunar(int year, int month, int day, int lunar_year,
                         int lunar_month, int lunar_day, bool leap)
{
    lunar_date_t result = {};
    assert(lunar_from_solar(year, month, day, &result));
    assert(result.year == lunar_year);
    assert(result.month == lunar_month);
    assert(result.day == lunar_day);
    assert(result.leap_month == leap);
}

int main(void)
{
    expect_lunar(2024, 2, 10, 2024, 1, 1, false);
    expect_lunar(2025, 1, 29, 2025, 1, 1, false);
    expect_lunar(2026, 2, 17, 2026, 1, 1, false);

    assert(strcmp(solar_term_for_date(2024, 4, 4), "清明") == 0);
    assert(strcmp(solar_term_for_date(2024, 12, 21), "冬至") == 0);
    assert(strcmp(festival_for_date(2024, 2, 10), "春节") == 0);
    assert(strcmp(festival_for_date(2024, 9, 17), "中秋") == 0);

    char text[32] = {0};
    lunar_calendar_cell_text(2024, 2, 10, text, sizeof(text));
    assert(strcmp(text, "春节") == 0);

    lunar_date_t invalid = {};
    assert(!lunar_from_solar(2026, 2, 29, &invalid));
    assert(!lunar_from_solar(2026, 4, 31, &invalid));
    assert(!lunar_from_solar(1901, 1, 1, &invalid));
    assert(!lunar_from_solar(2100, 1, 1, &invalid));
    assert(lunar_from_solar(2024, 2, 29, &invalid));
    assert(lunar_day_of_week(2026, 0, 1) == -1);
    assert(lunar_day_of_week(2026, 13, 1) == -1);
    assert(lunar_day_of_week(2026, 2, 29) == -1);
    // Walk the supported table to detect out-of-range lunar table results.
    for (int year = 1902; year <= 2099; ++year) {
        for (int month = 1; month <= 12; ++month) {
            for (int day = 1; day <= 31; ++day) {
                lunar_date_t result = {};
                if (!lunar_from_solar(year, month, day, &result)) continue;
                assert(result.month >= 1 && result.month <= 12);
                assert(result.day >= 1 && result.day <= 30);
                assert(result.year == year || result.year == year - 1);
            }
        }
    }
    puts("lunar tests passed");
    return 0;
}
