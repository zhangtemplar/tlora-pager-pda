/**
 * @file      ui_calendar.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-06
 * @brief     Calendar app with RTC date, US Federal & Chinese Traditional holidays.
 */
#include "ui_define.h"
#include "lunar_calendar.h"

static lv_obj_t *menu = NULL;
static lv_obj_t *quit_btn = NULL;
static lv_obj_t *calendar_obj = NULL;
static lv_obj_t *holiday_label = NULL;
static lv_calendar_date_t highlighted_days_buf[31];
static int highlighted_count = 0;

static int current_year = 2025;
static int current_month = 1;

// Holiday cache for the current month — avoids re-scanning all days on every call
static int cached_year = 0;
static int cached_month = 0;
static int cached_count = 0;
static int cached_days[31];
static const char *cached_names[31];

static void cache_holidays(int year, int month)
{
    if (year == cached_year && month == cached_month) return;
    cached_year = year;
    cached_month = month;
    cached_count = get_month_holidays(year, month, cached_days, cached_names);
}

// Look up a holiday name from the cache (avoids per-day get_holiday_name calls)
static const char *cached_holiday_name(int year, int month, int day)
{
    if (year != cached_year || month != cached_month) cache_holidays(year, month);
    for (int i = 0; i < cached_count; i++) {
        if (cached_days[i] == day) return cached_names[i];
    }
    return NULL;
}

static void update_holidays(int year, int month)
{
    cache_holidays(year, month);
    highlighted_count = cached_count;

    for (int i = 0; i < highlighted_count && i < 31; i++) {
        highlighted_days_buf[i].year = year;
        highlighted_days_buf[i].month = month;
        highlighted_days_buf[i].day = cached_days[i];
    }

    if (highlighted_count > 0) {
        lv_calendar_set_highlighted_dates(calendar_obj, highlighted_days_buf, highlighted_count);
        if (holiday_label) {
            // Build marquee string: "bell d1: Name1  |  d2: Name2  |  ..."
            char buf[256];
            int pos = snprintf(buf, sizeof(buf), "%d/%d %s ", year, month, LV_SYMBOL_BELL);
            for (int i = 0; i < highlighted_count && pos < (int)sizeof(buf) - 1; i++) {
                if (i > 0) pos += snprintf(buf + pos, sizeof(buf) - pos, "  |  ");
                pos += snprintf(buf + pos, sizeof(buf) - pos, "%d: %s",
                                cached_days[i], cached_names[i]);
            }
            lv_label_set_text(holiday_label, buf);
        }
    } else {
        lv_calendar_set_highlighted_dates(calendar_obj, NULL, 0);
        if (holiday_label) {
            lv_label_set_text_fmt(holiday_label, "%d/%d - No holidays", year, month);
        }
    }
}

static void navigate_month(int delta)
{
    current_month += delta;
    if (current_month > 12) { current_month = 1; current_year++; }
    if (current_month < 1)  { current_month = 12; current_year--; }
    lv_calendar_set_showed_date(calendar_obj, current_year, current_month);
    update_holidays(current_year, current_month);
}

static void navigate_year(int delta)
{
    current_year += delta;
    lv_calendar_set_showed_date(calendar_obj, current_year, current_month);
    update_holidays(current_year, current_month);
}

static void calendar_keyboard_cb(int state, char &c)
{
    if (state != 1) return;

    // Space+Q ('1') -> back/quit
    if (c == '1') {
        if (menu) {
            lv_obj_t *back_btn = lv_menu_get_main_header_back_button(menu);
            if (back_btn) lv_obj_send_event(back_btn, LV_EVENT_CLICKED, NULL);
        }
        c = 0;
        return;
    }

    // Space+W ('2') -> previous month
    if (c == '2') {
        navigate_month(-1);
        c = 0;
        return;
    }

    // Space+S ('/') -> next month
    if (c == '/') {
        navigate_month(1);
        c = 0;
        return;
    }

    // Space+P ('0') -> previous year
    if (c == '0') {
        navigate_year(-1);
        c = 0;
        return;
    }

    // Space+N (',') -> next year
    if (c == ',') {
        navigate_year(1);
        c = 0;
        return;
    }
}

static void calendar_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);

    if (code == LV_EVENT_VALUE_CHANGED) {
        lv_calendar_date_t date;
        if (lv_calendar_get_pressed_date(obj, &date) == LV_RESULT_OK) {
            // Check if month changed
            if (date.year != current_year || date.month != current_month) {
                current_year = date.year;
                current_month = date.month;
                update_holidays(current_year, current_month);
            }

            const char *name = cached_holiday_name(date.year, date.month, date.day);
            if (name) {
                lv_label_set_text_fmt(holiday_label, "%d/%d %s %d: %s",
                                      date.year, date.month, LV_SYMBOL_BELL, date.day, name);
            } else {
                lv_label_set_text_fmt(holiday_label, "%d/%d/%d",
                                      date.year, date.month, date.day);
            }
        }
    }
}

static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_btn_is_root(menu, obj)) {
        hw_set_keyboard_read_callback(NULL);

        lv_obj_clean(menu);
        lv_obj_del(menu);

        if (quit_btn) {
            lv_obj_del_async(quit_btn);
            quit_btn = NULL;
        }

        menu_show();
    }
}

void ui_calendar_enter(lv_obj_t *parent)
{
    menu = create_menu(parent, back_event_handler);

    lv_obj_t *main_page = lv_menu_page_create(menu, NULL);

    // Get current date from RTC
    struct tm timeinfo;
    hw_get_date_time(timeinfo);
    int year = timeinfo.tm_year + 1900;
    int month = timeinfo.tm_mon + 1;
    int day = timeinfo.tm_mday;

    // Sanity check
    if (year < 2020 || year > 2099) { year = 2025; month = 1; day = 1; }

    current_year = year;
    current_month = month;

    // Create calendar widget (no dropdown/arrow header — keyboard shortcuts navigate)
    calendar_obj = lv_calendar_create(main_page);
    lv_obj_set_size(calendar_obj, lv_pct(100), lv_pct(90));
    lv_obj_align(calendar_obj, LV_ALIGN_TOP_MID, 0, 0);

    lv_calendar_set_today_date(calendar_obj, year, month, day);
    lv_calendar_set_showed_date(calendar_obj, year, month);

    lv_obj_add_event_cb(calendar_obj, calendar_event_handler, LV_EVENT_VALUE_CHANGED, NULL);

    // Holiday info label at bottom
    holiday_label = lv_label_create(main_page);
    lv_obj_set_width(holiday_label, lv_pct(100));
    lv_obj_set_style_text_align(holiday_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(holiday_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(holiday_label, "Select a date to see holidays");

    // Highlight holidays for current month
    update_holidays(year, month);

    lv_menu_set_page(menu, main_page);

#ifdef USING_TOUCHPAD
    quit_btn = create_floating_button([](lv_event_t *e) {
        lv_obj_send_event(lv_menu_get_main_header_back_button(menu), LV_EVENT_CLICKED, NULL);
    }, NULL);
#endif

    enable_keyboard();
    hw_set_keyboard_read_callback(calendar_keyboard_cb);
}


void ui_calendar_exit(lv_obj_t *parent)
{
    cached_year = 0;
    cached_month = 0;
}

app_t ui_calendar_main = {
    .setup_func_cb = ui_calendar_enter,
    .exit_func_cb = ui_calendar_exit,
    .user_data = nullptr,
};
