/*
 * Lunar conversion table and algorithm adapted from:
 * https://github.com/illusionlee/lunar_calendar
 * Copyright (c) 2019 Illusion Lee
 * SPDX-License-Identifier: MIT
 */

#include "lunar.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char *s_lunar_days[] = {
    "",     "初一", "初二", "初三", "初四", "初五", "初六", "初七",
    "初八", "初九", "初十", "十一", "十二", "十三", "十四", "十五",
    "十六", "十七", "十八", "十九", "二十", "廿一", "廿二", "廿三",
    "廿四", "廿五", "廿六", "廿七", "廿八", "廿九", "三十",
};

static const char *s_lunar_months[] = {
    "", "正月", "二月", "三月", "四月", "五月", "六月",
    "七月", "八月", "九月", "十月", "冬月", "腊月",
};

static const char *s_weekdays[] = {
    "星期日", "星期一", "星期二", "星期三",
    "星期四", "星期五", "星期六",
};

static const char *s_solar_terms[] = {
    "小寒", "大寒", "立春", "雨水", "惊蛰", "春分", "清明", "谷雨",
    "立夏", "小满", "芒种", "夏至", "小暑", "大暑", "立秋", "处暑",
    "白露", "秋分", "寒露", "霜降", "立冬", "小雪", "大雪", "冬至",
};

static const int s_solar_term_minutes[] = {
    0,      21208,  42467,  63836,  85337,  107014, 128867, 150921,
    173149, 195551, 218072, 240693, 263343, 285989, 308563, 331033,
    353350, 375494, 397447, 419210, 440795, 462224, 483532, 504758,
};

static const uint32_t s_lunar_table[] = {
    0x04AE53, 0x0A5748, 0x5526BD, 0x0D2650, 0x0D9544, 0x46AAB9,
    0x056A4D, 0x09AD42, 0x24AEB6, 0x04AE4A, 0x6A4DBE, 0x0A4D52,
    0x0D2546, 0x5D52BA, 0x0B544E, 0x0D6A43, 0x296D37, 0x095B4B,
    0x749BC1, 0x049754, 0x0A4B48, 0x5B25BC, 0x06A550, 0x06D445,
    0x4ADAB8, 0x02B64D, 0x095742, 0x2497B7, 0x04974A, 0x664B3E,
    0x0D4A51, 0x0EA546, 0x56D4BA, 0x05AD4E, 0x02B644, 0x393738,
    0x092E4B, 0x7C96BF, 0x0C9553, 0x0D4A48, 0x6DA53B, 0x0B554F,
    0x056A45, 0x4AADB9, 0x025D4D, 0x092D42, 0x2C95B6, 0x0A954A,
    0x7B4ABD, 0x06CA51, 0x0B5546, 0x555ABB, 0x04DA4E, 0x0A5B43,
    0x352BB8, 0x052B4C, 0x8A953F, 0x0E9552, 0x06AA48, 0x6AD53C,
    0x0AB54F, 0x04B645, 0x4A5739, 0x0A574D, 0x052642, 0x3E9335,
    0x0D9549, 0x75AABE, 0x056A51, 0x096D46, 0x54AEBB, 0x04AD4F,
    0x0A4D43, 0x4D26B7, 0x0D254B, 0x8D52BF, 0x0B5452, 0x0B6A47,
    0x696D3C, 0x095B50, 0x049B45, 0x4A4BB9, 0x0A4B4D, 0xAB25C2,
    0x06A554, 0x06D449, 0x6ADA3D, 0x0AB651, 0x093746, 0x5497BB,
    0x04974F, 0x064B44, 0x36A537, 0x0EA54A, 0x86B2BF, 0x05AC53,
    0x0AB647, 0x5936BC, 0x092E50, 0x0C9645, 0x4D4AB8, 0x0D4A4C,
    0x0DA541, 0x25AAB6, 0x056A49, 0x7AADBD, 0x025D52, 0x092D47,
    0x5C95BA, 0x0A954E, 0x0B4A43, 0x4B5537, 0x0AD54A, 0x955ABF,
    0x04BA53, 0x0A5B48, 0x652BBC, 0x052B50, 0x0A9345, 0x474AB9,
    0x06AA4C, 0x0AD541, 0x24DAB6, 0x04B64A, 0x69573D, 0x0A4E51,
    0x0D2646, 0x5E933A, 0x0D534D, 0x05AA43, 0x36B537, 0x096D4B,
    0xB4AEBF, 0x04AD53, 0x0A4D48, 0x6D25BC, 0x0D254F, 0x0D5244,
    0x5DAA38, 0x0B5A4C, 0x056D41, 0x24ADB6, 0x049B4A, 0x7A4BBE,
    0x0A4B51, 0x0AA546, 0x5B52BA, 0x06D24E, 0x0ADA42, 0x355B37,
    0x09374B, 0x8497C1, 0x049753, 0x064B48, 0x66A53C, 0x0EA54F,
    0x06B244, 0x4AB638, 0x0AAE4C, 0x092E42, 0x3C9735, 0x0C9649,
    0x7D4ABD, 0x0D4A51, 0x0DA545, 0x55AABA, 0x056A4E, 0x0A6D43,
    0x452EB7, 0x052D4B, 0x8A95BF, 0x0A9553, 0x0B4A47, 0x6B553B,
    0x0AD54F, 0x055A45, 0x4A5D38, 0x0A5B4C, 0x052B42, 0x3A93B6,
    0x069349, 0x7729BD, 0x06AA51, 0x0AD546, 0x54DABA, 0x04B64E,
    0x0A5743, 0x452738, 0x0D264A, 0x8E933E, 0x0D5252, 0x0DAA47,
    0x66B53B, 0x056D4F, 0x04AE45, 0x4A4EB9, 0x0A4D4C, 0x0D1541,
    0x2D92B5,
};

static int64_t civil_days_from_epoch(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned year_of_era = (unsigned)(year - era * 400);
    const unsigned day_of_year =
        (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned day_of_era =
        year_of_era * 365 + year_of_era / 4 - year_of_era / 100 +
        day_of_year;
    return (int64_t)era * 146097 + (int64_t)day_of_era - 719468;
}

static void civil_from_days(int64_t days, int *year, int *month, int *day)
{
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned day_of_era = (unsigned)(days - era * 146097);
    const unsigned year_of_era =
        (day_of_era - day_of_era / 1460 + day_of_era / 36524 -
         day_of_era / 146096) /
        365;
    int y = (int)year_of_era + (int)era * 400;
    const unsigned day_of_year =
        day_of_era -
        (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    const unsigned month_prime = (5 * day_of_year + 2) / 153;
    const unsigned d =
        day_of_year - (153 * month_prime + 2) / 5 + 1;
    const unsigned m = month_prime + (month_prime < 10 ? 3 : -9);
    y += m <= 2;
    *year = y;
    *month = (int)m;
    *day = (int)d;
}

static int solar_term_day(int year, int index)
{
    if (index < 0 || index >= 24 || year < 1900) {
        return -1;
    }

    const int64_t base_days = civil_days_from_epoch(1900, 1, 6);
    const int64_t minutes =
        base_days * 1440 + 125 +
        (int64_t)(525948.766245 * (year - 1900) + 0.5) +
        s_solar_term_minutes[index];
    int64_t day_number = minutes / 1440;
    if (minutes < 0 && minutes % 1440 != 0) {
        --day_number;
    }
    int term_year = 0;
    int term_month = 0;
    int term_day = 0;
    civil_from_days(day_number, &term_year, &term_month, &term_day);
    if (term_year != year || term_month != 1 + index / 2) {
        return -1;
    }
    return term_day;
}

bool lunar_from_solar(int year, int month, int day, lunar_date_t *out)
{
    if (out == NULL || year < 1902 || year > 2099 || month < 1 ||
        month > 12 || day < 1 || day > 31) {
        return false;
    }

    const uint32_t info = s_lunar_table[year - 1901];
    int lunar_new_year =
        (int)(info & 0x1f) - 1 + (((info & 0x60) >> 5) == 1 ? 0 : 31);

    static const int month_starts[] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334,
    };
    int solar_days = month_starts[month - 1] + day - 1;
    const bool gregorian_leap =
        ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0);
    if (gregorian_leap && month > 2) {
        ++solar_days;
    }

    int lunar_year = year;
    int lunar_month;
    int lunar_day;
    int month_index;
    int flag = 0;
    uint32_t active_info = info;

    if (solar_days >= lunar_new_year) {
        solar_days -= lunar_new_year;
        lunar_month = 1;
        month_index = 1;
        int month_days =
            (info & (0x80000U >> (month_index - 1))) == 0 ? 29 : 30;
        while (solar_days >= month_days) {
            solar_days -= month_days;
            ++month_index;
            if (lunar_month == (int)((info & 0xf00000U) >> 20)) {
                flag = ~flag;
                if (flag == 0) {
                    ++lunar_month;
                }
            } else {
                ++lunar_month;
            }
            month_days =
                (info & (0x80000U >> (month_index - 1))) == 0 ? 29 : 30;
        }
        lunar_day = solar_days + 1;
    } else {
        lunar_new_year -= solar_days;
        --lunar_year;
        lunar_month = 12;
        const uint32_t previous_info = s_lunar_table[lunar_year - 1901];
        active_info = previous_info;
        month_index =
            ((previous_info & 0xf00000U) >> 20) == 0 ? 12 : 13;
        int month_days =
            (previous_info & (0x80000U >> (month_index - 1))) == 0 ? 29 : 30;
        while (lunar_new_year > month_days) {
            lunar_new_year -= month_days;
            --month_index;
            if (flag == 0) {
                --lunar_month;
            }
            if (lunar_month == (int)((previous_info & 0xf00000U) >> 20)) {
                flag = ~flag;
            }
            month_days =
                (previous_info & (0x80000U >> (month_index - 1))) == 0 ? 29
                                                                       : 30;
        }
        lunar_day = month_days - lunar_new_year + 1;
    }

    out->year = lunar_year;
    out->month = lunar_month;
    out->day = lunar_day;
    out->leap_month =
        flag != 0 &&
        lunar_month == (int)((active_info & 0xf00000U) >> 20);
    return true;
}

int lunar_day_of_week(int year, int month, int day)
{
    static const int offsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 3) {
        --year;
    }
    return (year + year / 4 - year / 100 + year / 400 +
            offsets[month - 1] + day) %
           7;
}

const char *lunar_month_name(int month, bool leap_month)
{
    static char buffer[16];
    if (month < 1 || month > 12) {
        return "";
    }
    snprintf(buffer, sizeof(buffer), "%s%s", leap_month ? "闰" : "",
             s_lunar_months[month]);
    return buffer;
}

const char *lunar_day_name(int day)
{
    if (day < 1 || day > 30) {
        return "";
    }
    return s_lunar_days[day];
}

const char *solar_term_for_date(int year, int month, int day)
{
    if (month < 1 || month > 12) {
        return NULL;
    }
    const int first = (month - 1) * 2;
    if (solar_term_day(year, first) == day) {
        return s_solar_terms[first];
    }
    if (solar_term_day(year, first + 1) == day) {
        return s_solar_terms[first + 1];
    }
    return NULL;
}

const char *festival_for_date(int year, int month, int day)
{
    if (month == 1 && day == 1) {
        return "元旦";
    }
    if (month == 5 && day == 1) {
        return "劳动节";
    }
    if (month == 10 && day == 1) {
        return "国庆节";
    }

    lunar_date_t lunar = {};
    if (!lunar_from_solar(year, month, day, &lunar) || lunar.leap_month) {
        return NULL;
    }
    if (lunar.month == 1 && lunar.day == 1) {
        return "春节";
    }
    if (lunar.month == 1 && lunar.day == 15) {
        return "元宵";
    }
    if (lunar.month == 5 && lunar.day == 5) {
        return "端午";
    }
    if (lunar.month == 7 && lunar.day == 7) {
        return "七夕";
    }
    if (lunar.month == 8 && lunar.day == 15) {
        return "中秋";
    }
    if (lunar.month == 9 && lunar.day == 9) {
        return "重阳";
    }
    if (lunar.month == 12 && lunar.day == 8) {
        return "腊八";
    }
    return NULL;
}

const char *weekday_name(int weekday)
{
    if (weekday < 0 || weekday > 6) {
        return "";
    }
    return s_weekdays[weekday];
}

void lunar_format_full(int year, int month, int day, char *out, size_t size)
{
    if (out == NULL || size == 0) {
        return;
    }
    lunar_date_t lunar = {};
    if (!lunar_from_solar(year, month, day, &lunar)) {
        snprintf(out, size, "农历数据超出范围");
        return;
    }
    snprintf(out, size, "农历%s%s", lunar_month_name(lunar.month, lunar.leap_month),
             lunar_day_name(lunar.day));
}

void lunar_calendar_cell_text(int year, int month, int day, char *out,
                              size_t size)
{
    if (out == NULL || size == 0) {
        return;
    }

    const char *festival = festival_for_date(year, month, day);
    if (festival != NULL) {
        snprintf(out, size, "%s", festival);
        return;
    }

    const char *term = solar_term_for_date(year, month, day);
    if (term != NULL) {
        snprintf(out, size, "%s", term);
        return;
    }

    lunar_date_t lunar = {};
    if (!lunar_from_solar(year, month, day, &lunar)) {
        out[0] = '\0';
        return;
    }
    if (lunar.day == 1 || lunar.month == 1) {
        snprintf(out, size, "%s", lunar_month_name(lunar.month, lunar.leap_month));
    } else {
        snprintf(out, size, "%s", lunar_day_name(lunar.day));
    }
}
