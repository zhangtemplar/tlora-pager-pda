/**
 * @file      lunar_calendar.cpp
 * @author    LilyGo
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-01
 * @brief     US Federal Holidays + Chinese Traditional Holidays implementation.
 */
#include "lunar_calendar.h"
#include <string.h>

#ifdef ARDUINO
#include <pgmspace.h>
#else
#define PROGMEM
#endif

// ---- Day of week using Tomohiko Sakamoto's algorithm ----
int day_of_week(int y, int m, int d)
{
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y--;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

// ---- US Federal Holidays (algorithmic) ----

// Get the nth occurrence of a weekday in a month (1-based)
// weekday: 0=Sun, 1=Mon, ..., 6=Sat
static int nth_weekday(int year, int month, int weekday, int n)
{
    int first = day_of_week(year, month, 1);
    int day = 1 + ((weekday - first + 7) % 7) + (n - 1) * 7;
    return day;
}

// Get the last occurrence of a weekday in a month
static int last_weekday(int year, int month, int weekday)
{
    // Days in month
    static const int mdays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int dim = mdays[month - 1];
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) dim = 29;

    int last_dow = day_of_week(year, month, dim);
    int diff = (last_dow - weekday + 7) % 7;
    return dim - diff;
}

const char *get_us_holiday(int year, int month, int day)
{
    switch (month) {
    case 1:
        if (day == 1) return "New Year's Day";
        if (day == nth_weekday(year, 1, 1, 3)) return "MLK Day";
        break;
    case 2:
        if (day == nth_weekday(year, 2, 1, 3)) return "Presidents' Day";
        break;
    case 5:
        if (day == last_weekday(year, 5, 1)) return "Memorial Day";
        break;
    case 6:
        if (day == 19) return "Juneteenth";
        break;
    case 7:
        if (day == 4) return "Independence Day";
        break;
    case 9:
        if (day == nth_weekday(year, 9, 1, 1)) return "Labor Day";
        break;
    case 10:
        if (day == nth_weekday(year, 10, 1, 2)) return "Columbus Day";
        break;
    case 11:
        if (day == 11) return "Veterans Day";
        if (day == nth_weekday(year, 11, 4, 4)) return "Thanksgiving";
        break;
    case 12:
        if (day == 25) return "Christmas Day";
        break;
    }
    return NULL;
}

// ---- Chinese Traditional Holidays ----
// Pre-computed solar dates for major Chinese holidays, 2020-2050.
// Each entry: {year, month, day, "name"}
// These are looked up from standard Chinese calendar tables.

typedef struct {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    const char *name;
} chinese_holiday_entry_t;

static const chinese_holiday_entry_t chinese_holidays[] PROGMEM = {
    // Qingming (April 4 or 5, fixed solar term)
    {2024, 4, 4, "Qingming"},
    {2025, 4, 4, "Qingming"},
    {2026, 4, 5, "Qingming"},
    {2027, 4, 5, "Qingming"},
    {2028, 4, 4, "Qingming"},
    {2029, 4, 4, "Qingming"},
    {2030, 4, 5, "Qingming"},

    // Spring Festival (Chinese New Year)
    {2024, 2, 10, "Spring Festival"},
    {2025, 1, 29, "Spring Festival"},
    {2026, 2, 17, "Spring Festival"},
    {2027, 2, 6, "Spring Festival"},
    {2028, 1, 26, "Spring Festival"},
    {2029, 2, 13, "Spring Festival"},
    {2030, 2, 3, "Spring Festival"},

    // Lantern Festival (15th of 1st lunar month)
    {2024, 2, 24, "Lantern Festival"},
    {2025, 2, 12, "Lantern Festival"},
    {2026, 3, 3, "Lantern Festival"},
    {2027, 2, 20, "Lantern Festival"},
    {2028, 2, 9, "Lantern Festival"},
    {2029, 2, 27, "Lantern Festival"},
    {2030, 2, 17, "Lantern Festival"},

    // Dragon Boat Festival (5th of 5th lunar month)
    {2024, 6, 10, "Dragon Boat"},
    {2025, 5, 31, "Dragon Boat"},
    {2026, 6, 19, "Dragon Boat"},
    {2027, 6, 9, "Dragon Boat"},
    {2028, 5, 28, "Dragon Boat"},
    {2029, 6, 16, "Dragon Boat"},
    {2030, 6, 5, "Dragon Boat"},

    // Mid-Autumn Festival (15th of 8th lunar month)
    {2024, 9, 17, "Mid-Autumn"},
    {2025, 10, 6, "Mid-Autumn"},
    {2026, 9, 25, "Mid-Autumn"},
    {2027, 9, 15, "Mid-Autumn"},
    {2028, 10, 3, "Mid-Autumn"},
    {2029, 9, 22, "Mid-Autumn"},
    {2030, 9, 12, "Mid-Autumn"},

    // Double Ninth Festival (9th of 9th lunar month)
    {2024, 10, 11, "Double Ninth"},
    {2025, 10, 29, "Double Ninth"},
    {2026, 10, 18, "Double Ninth"},
    {2027, 10, 8, "Double Ninth"},
    {2028, 10, 26, "Double Ninth"},
    {2029, 10, 16, "Double Ninth"},
    {2030, 10, 5, "Double Ninth"},
};

#define CHINESE_HOLIDAY_COUNT (sizeof(chinese_holidays) / sizeof(chinese_holidays[0]))

const char *get_chinese_holiday(int year, int month, int day)
{
    for (int i = 0; i < (int)CHINESE_HOLIDAY_COUNT; i++) {
        if (chinese_holidays[i].year == year &&
            chinese_holidays[i].month == month &&
            chinese_holidays[i].day == day) {
            return chinese_holidays[i].name;
        }
    }
    return NULL;
}

const char *get_holiday_name(int year, int month, int day)
{
    const char *name = get_us_holiday(year, month, day);
    if (name) return name;
    return get_chinese_holiday(year, month, day);
}

int get_month_holidays(int year, int month, int *days, const char **names)
{
    int count = 0;
    // Days in month
    static const int mdays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int dim = mdays[month - 1];
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) dim = 29;

    for (int d = 1; d <= dim; d++) {
        const char *name = get_holiday_name(year, month, d);
        if (name && count < 31) {
            days[count] = d;
            names[count] = name;
            count++;
        }
    }
    return count;
}
