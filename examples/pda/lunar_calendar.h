/**
 * @file      lunar_calendar.h
 * @author    LilyGo
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-01
 * @brief     Lunar calendar lookup table and holiday computation.
 */
#pragma once

#include <stdint.h>

/**
 * @brief Get the day of the week (0=Sun, 1=Mon, ..., 6=Sat) for a given date.
 */
int day_of_week(int year, int month, int day);

/**
 * @brief Check if a date is a US federal holiday.
 * @param year Year (e.g., 2025)
 * @param month Month (1-12)
 * @param day Day (1-31)
 * @return Holiday name string, or NULL if not a holiday.
 */
const char *get_us_holiday(int year, int month, int day);

/**
 * @brief Check if a date is a Chinese traditional holiday.
 * @param year Year (e.g., 2025)
 * @param month Month (1-12)
 * @param day Day (1-31) in solar/Gregorian calendar
 * @return Holiday name string, or NULL if not a holiday.
 */
const char *get_chinese_holiday(int year, int month, int day);

/**
 * @brief Get any holiday name for the given date (US or Chinese).
 * @return Holiday name string, or NULL if not a holiday.
 */
const char *get_holiday_name(int year, int month, int day);

/**
 * @brief Get the number of holidays in a given year+month, and fill arrays.
 * @param year Year
 * @param month Month (1-12)
 * @param days Output array of holiday days (must be at least 31 elements).
 * @param names Output array of holiday name pointers.
 * @return Number of holidays found.
 */
int get_month_holidays(int year, int month, int *days, const char **names);
