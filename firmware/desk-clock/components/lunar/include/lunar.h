#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int year;
    int month;
    int day;
    bool leap_month;
} lunar_date_t;

bool lunar_from_solar(int year, int month, int day, lunar_date_t *out);
int lunar_day_of_week(int year, int month, int day);

const char *lunar_month_name(int month, bool leap_month);
const char *lunar_day_name(int day);
const char *solar_term_for_date(int year, int month, int day);
const char *festival_for_date(int year, int month, int day);
const char *weekday_name(int weekday);

void lunar_format_full(int year, int month, int day, char *out, size_t size);
void lunar_calendar_cell_text(int year, int month, int day, char *out,
                              size_t size);

#ifdef __cplusplus
}
#endif
