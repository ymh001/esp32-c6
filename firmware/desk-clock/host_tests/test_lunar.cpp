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

    puts("lunar tests passed");
    return 0;
}
