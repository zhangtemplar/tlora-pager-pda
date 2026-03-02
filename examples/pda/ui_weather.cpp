/**
 * @file      ui_weather.cpp
 * @author    LilyGo
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-01
 * @brief     Weather app using OpenWeatherMap One Call API 3.0.
 *            Single page, scrolling via:
 *            - Encoder (scroll wheel): items in group, auto-scroll on focus
 *            - Keyboard: Space+W/S direct scroll via lv_obj_scroll_by()
 *            Caches weather data for 1 hour.
 */
#include "ui_define.h"
#include "config_keys.h"
#include "http_utils.h"

#ifdef ARDUINO
#include <cJSON.h>
#include <Preferences.h>
#endif

// Weather icon declarations
LV_IMG_DECLARE(img_w_clear);
LV_IMG_DECLARE(img_w_pcloudy);
LV_IMG_DECLARE(img_w_cloud);
LV_IMG_DECLARE(img_w_rain);
LV_IMG_DECLARE(img_w_storm);
LV_IMG_DECLARE(img_w_snow);
LV_IMG_DECLARE(img_w_mist);

static lv_obj_t *menu = NULL;
static lv_obj_t *quit_btn = NULL;
static lv_timer_t *refresh_timer = NULL;
static TaskHandle_t fetch_task = NULL;
static lv_obj_t *main_page = NULL;
static bool forecast_ui_built = false;

// UI elements
static lv_obj_t *city_label = NULL;
static lv_obj_t *updated_label = NULL;
static lv_obj_t *temp_label = NULL;
static lv_obj_t *detail_label = NULL;
static lv_obj_t *desc_label = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *weather_icon = NULL;

// --- Data structures ---

struct current_weather_t {
    float temp;
    float feels_like;
    int humidity;
    float wind;
    int pressure;
    char desc[64];
    char icon[8];
    int visibility;
    int uvi;
};

struct hourly_entry_t {
    char time_str[6];
    char desc[24];
    float temp;
    int humidity;
    float wind;
    int pop_pct;
};

struct daily_entry_t {
    char day_str[6];
    char desc[24];
    float temp_min;
    float temp_max;
    int humidity;
    float wind;
    int pop_pct;
};

#define MAX_HOURLY 24
#define MAX_DAILY 8

static current_weather_t cur = {};
static char location_name[64] = "";
static hourly_entry_t hourly[MAX_HOURLY] = {};
static daily_entry_t daily[MAX_DAILY] = {};
static int hourly_count = 0;
static int daily_count = 0;
static bool data_valid = false;
static uint32_t last_fetch_time = 0;

// --- Weather icon mapping ---

static const lv_img_dsc_t *weather_icon_img(const char *icon_code)
{
    if (!icon_code || !icon_code[0]) return &img_w_cloud;
    char c0 = icon_code[0], c1 = icon_code[1];
    if (c0 == '0' && c1 == '1') return &img_w_clear;
    if (c0 == '0' && c1 == '2') return &img_w_pcloudy;
    if (c0 == '0' && c1 == '3') return &img_w_cloud;
    if (c0 == '0' && c1 == '4') return &img_w_cloud;
    if (c0 == '0' && c1 == '9') return &img_w_rain;
    if (c0 == '1' && c1 == '0') return &img_w_rain;
    if (c0 == '1' && c1 == '1') return &img_w_storm;
    if (c0 == '1' && c1 == '3') return &img_w_snow;
    if (c0 == '5' && c1 == '0') return &img_w_mist;
    return &img_w_cloud;
}

static void capitalize(char *s)
{
    if (s && s[0] >= 'a' && s[0] <= 'z') s[0] -= 32;
}

// --- Keyboard callback for Space+key shortcuts ---
// Scrolling is clamped to content bounds to prevent empty space.
static void weather_keyboard_cb(int state, char &c)
{
    if (state != 1) return;

    // Space+Q ('1') → back/quit
    if (c == '1') {
        if (menu) {
            lv_obj_t *back_btn = lv_menu_get_main_header_back_button(menu);
            if (back_btn) lv_obj_send_event(back_btn, LV_EVENT_CLICKED, NULL);
        }
        c = 0;
        return;
    }

    if (!main_page) return;

    int page_h = lv_obj_get_height(main_page);
    if (page_h < 40) page_h = 200;
    int top = lv_obj_get_scroll_top(main_page);
    int bottom = lv_obj_get_scroll_bottom(main_page);
    int dy = 0;

    switch (c) {
    case '2':  // Space+W → scroll up (content moves down)
        dy = (top > 30) ? 30 : top;
        break;
    case '/':  // Space+S → scroll down (content moves up)
        dy = (bottom > 30) ? -30 : -bottom;
        break;
    case '0':  // Space+P → page up
        dy = (top > page_h - 20) ? (page_h - 20) : top;
        break;
    case ',':  // Space+N → page down
        dy = (bottom > page_h - 20) ? -(page_h - 20) : -bottom;
        break;
    default:
        return;
    }

    if (dy != 0) lv_obj_scroll_by(main_page, 0, dy, LV_ANIM_ON);
    c = 0;
}

// --- Build forecast entries (called once when data arrives) ---

static void build_forecast_ui()
{
    if (forecast_ui_built) return;
    if (!data_valid || !main_page) return;

    forecast_ui_built = true;
    lv_group_t *g = lv_group_get_default();

    // --- Section: 24-Hour Forecast ---
    lv_obj_t *h_hdr = lv_menu_cont_create(main_page);
    lv_obj_t *h_lbl = lv_label_create(h_hdr);
    lv_label_set_text(h_lbl, "-- 24-Hour Forecast --");
    lv_obj_set_style_text_color(h_lbl, lv_color_make(100, 200, 255), 0);
    if (g) lv_group_add_obj(g, h_hdr);

    // Column header: Time  Desc  Temp  Hum  Wind  Rain
    lv_obj_t *hc = lv_menu_cont_create(main_page);
    lv_obj_t *hcl = lv_label_create(hc);
    lv_label_set_text(hcl, "Time  Wthr  Temp Hum  Wind Rain");
    lv_obj_set_style_text_color(hcl, lv_color_make(180, 180, 180), 0);

    for (int i = 0; i < hourly_count; i++) {
        lv_obj_t *cont = lv_menu_cont_create(main_page);
        lv_obj_t *label = lv_label_create(cont);
        char buf[64];
        snprintf(buf, sizeof(buf), "%s %-5s %3.0f\xC2\xB0 %3d%% %4.1f %3d%%",
                 hourly[i].time_str, hourly[i].desc, hourly[i].temp,
                 hourly[i].humidity, hourly[i].wind, hourly[i].pop_pct);
        lv_label_set_text(label, buf);
    }

    // --- Section: 8-Day Forecast ---
    lv_obj_t *d_hdr = lv_menu_cont_create(main_page);
    lv_obj_t *d_lbl = lv_label_create(d_hdr);
    lv_label_set_text(d_lbl, "-- 8-Day Forecast --");
    lv_obj_set_style_text_color(d_lbl, lv_color_make(100, 200, 255), 0);
    if (g) lv_group_add_obj(g, d_hdr);

    // Column header: Day  Desc  Lo/Hi  Hum  Wind  Rain
    lv_obj_t *dc = lv_menu_cont_create(main_page);
    lv_obj_t *dcl = lv_label_create(dc);
    lv_label_set_text(dcl, "Day   Wthr  Lo/Hi Hum  Wind Rain");
    lv_obj_set_style_text_color(dcl, lv_color_make(180, 180, 180), 0);

    for (int i = 0; i < daily_count; i++) {
        lv_obj_t *cont = lv_menu_cont_create(main_page);
        lv_obj_t *label = lv_label_create(cont);
        char buf[64];
        snprintf(buf, sizeof(buf), "%s %-5s %2.0f/%2.0f %3d%% %4.1f %3d%%",
                 daily[i].day_str, daily[i].desc,
                 daily[i].temp_min, daily[i].temp_max,
                 daily[i].humidity, daily[i].wind, daily[i].pop_pct);
        lv_label_set_text(label, buf);
    }
    printf("[Weather] forecast UI built: %d hourly + %d daily\n",
           hourly_count, daily_count);
}

// --- UI update ---

static void update_ui()
{
    if (!data_valid) return;

    if (city_label) lv_label_set_text(city_label, location_name);

    struct tm timeinfo;
    hw_get_date_time(timeinfo);
    if (updated_label)
        lv_label_set_text_fmt(updated_label, "Updated %02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);

    if (weather_icon)
        lv_image_set_src(weather_icon, weather_icon_img(cur.icon));

    capitalize(cur.desc);
    if (temp_label)
        lv_label_set_text_fmt(temp_label, "%.0f\xC2\xB0" "C  (%.0f\xC2\xB0)",
                              cur.temp, cur.feels_like);

    if (desc_label)
        lv_label_set_text(desc_label, cur.desc);

    if (detail_label)
        lv_label_set_text_fmt(detail_label,
                              "Hum: %d%%  Wind: %.1fm/s\n"
                              "Press: %dhPa  UV: %d",
                              cur.humidity, cur.wind,
                              cur.pressure, cur.uvi);

    if (status_label)
        lv_label_set_text(status_label, "");

    build_forecast_ui();
}

#ifdef ARDUINO

static void parse_onecall(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        printf("[Weather] JSON parse failed\n");
        return;
    }

    const char *day_names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

    cJSON *current = cJSON_GetObjectItem(root, "current");
    if (current) {
        cJSON *t = cJSON_GetObjectItem(current, "temp");
        if (t) cur.temp = t->valuedouble;
        cJSON *fl = cJSON_GetObjectItem(current, "feels_like");
        if (fl) cur.feels_like = fl->valuedouble;
        cJSON *hum = cJSON_GetObjectItem(current, "humidity");
        if (hum) cur.humidity = hum->valueint;
        cJSON *pres = cJSON_GetObjectItem(current, "pressure");
        if (pres) cur.pressure = pres->valueint;
        cJSON *ws = cJSON_GetObjectItem(current, "wind_speed");
        if (ws) cur.wind = ws->valuedouble;
        cJSON *vis = cJSON_GetObjectItem(current, "visibility");
        if (vis) cur.visibility = vis->valueint;
        cJSON *uvi = cJSON_GetObjectItem(current, "uvi");
        if (uvi) cur.uvi = (int)uvi->valuedouble;

        cJSON *weather_arr = cJSON_GetObjectItem(current, "weather");
        if (weather_arr && cJSON_GetArraySize(weather_arr) > 0) {
            cJSON *w0 = cJSON_GetArrayItem(weather_arr, 0);
            cJSON *desc = cJSON_GetObjectItem(w0, "description");
            if (desc && desc->valuestring) strncpy(cur.desc, desc->valuestring, 63);
            cJSON *ic = cJSON_GetObjectItem(w0, "icon");
            if (ic && ic->valuestring) strncpy(cur.icon, ic->valuestring, 7);
        }

        data_valid = true;
    }

    cJSON *hourly_arr = cJSON_GetObjectItem(root, "hourly");
    if (hourly_arr) {
        int count = cJSON_GetArraySize(hourly_arr);
        hourly_count = 0;
        for (int i = 0; i < count && hourly_count < MAX_HOURLY; i++) {
            cJSON *item = cJSON_GetArrayItem(hourly_arr, i);
            cJSON *dt = cJSON_GetObjectItem(item, "dt");
            if (!dt) continue;

            time_t ts = (time_t)dt->valueint;
            struct tm *tm_info = localtime(&ts);
            if (!tm_info) continue;

            hourly_entry_t *h = &hourly[hourly_count];
            snprintf(h->time_str, sizeof(h->time_str), "%02d:%02d",
                     tm_info->tm_hour, tm_info->tm_min);

            cJSON *t = cJSON_GetObjectItem(item, "temp");
            if (t) h->temp = t->valuedouble;
            cJSON *hum = cJSON_GetObjectItem(item, "humidity");
            if (hum) h->humidity = hum->valueint;
            cJSON *ws = cJSON_GetObjectItem(item, "wind_speed");
            if (ws) h->wind = ws->valuedouble;
            cJSON *pop = cJSON_GetObjectItem(item, "pop");
            if (pop) h->pop_pct = (int)(pop->valuedouble * 100);

            cJSON *weather_arr2 = cJSON_GetObjectItem(item, "weather");
            if (weather_arr2 && cJSON_GetArraySize(weather_arr2) > 0) {
                cJSON *w0 = cJSON_GetArrayItem(weather_arr2, 0);
                cJSON *main_str = cJSON_GetObjectItem(w0, "main");
                if (main_str && main_str->valuestring)
                    strncpy(h->desc, main_str->valuestring, 23);
            }

            hourly_count++;
        }
    }

    cJSON *daily_arr = cJSON_GetObjectItem(root, "daily");
    if (daily_arr) {
        int count = cJSON_GetArraySize(daily_arr);
        daily_count = 0;
        for (int i = 0; i < count && daily_count < MAX_DAILY; i++) {
            cJSON *item = cJSON_GetArrayItem(daily_arr, i);
            cJSON *dt = cJSON_GetObjectItem(item, "dt");
            if (!dt) continue;

            time_t ts = (time_t)dt->valueint;
            struct tm *tm_info = localtime(&ts);
            if (!tm_info) continue;

            daily_entry_t *d = &daily[daily_count];
            snprintf(d->day_str, sizeof(d->day_str), "%s",
                     day_names[tm_info->tm_wday]);

            cJSON *temp_obj = cJSON_GetObjectItem(item, "temp");
            if (temp_obj) {
                cJSON *tmin = cJSON_GetObjectItem(temp_obj, "min");
                cJSON *tmax = cJSON_GetObjectItem(temp_obj, "max");
                if (tmin) d->temp_min = tmin->valuedouble;
                if (tmax) d->temp_max = tmax->valuedouble;
            }

            cJSON *hum = cJSON_GetObjectItem(item, "humidity");
            if (hum) d->humidity = hum->valueint;
            cJSON *ws = cJSON_GetObjectItem(item, "wind_speed");
            if (ws) d->wind = ws->valuedouble;
            cJSON *pop = cJSON_GetObjectItem(item, "pop");
            if (pop) d->pop_pct = (int)(pop->valuedouble * 100);

            cJSON *weather_arr3 = cJSON_GetObjectItem(item, "weather");
            if (weather_arr3 && cJSON_GetArraySize(weather_arr3) > 0) {
                cJSON *w0 = cJSON_GetArrayItem(weather_arr3, 0);
                cJSON *main_str = cJSON_GetObjectItem(w0, "main");
                if (main_str && main_str->valuestring)
                    strncpy(d->desc, main_str->valuestring, 23);
            }

            daily_count++;
        }
    }

    cJSON_Delete(root);
}

static void fetch_city_name(float lat, float lon)
{
    char url[256];
    snprintf(url, sizeof(url),
             "https://api.openweathermap.org/geo/1.0/reverse?lat=%.4f&lon=%.4f&limit=1&appid=%s",
             lat, lon, OWM_API_KEY);

    http_response_t resp = http_get(url, 5000);
    if (resp.success) {
        cJSON *arr = cJSON_Parse(resp.body.c_str());
        if (arr && cJSON_IsArray(arr) && cJSON_GetArraySize(arr) > 0) {
            cJSON *item = cJSON_GetArrayItem(arr, 0);
            cJSON *name = cJSON_GetObjectItem(item, "name");
            if (name && name->valuestring) {
                strncpy(location_name, name->valuestring, 63);
            }
        }
        if (arr) cJSON_Delete(arr);
    }
}

static void save_cache()
{
    Preferences prefs;
    prefs.begin("weather", false);
    prefs.putBytes("cur", &cur, sizeof(cur));
    prefs.putString("locname", location_name);
    prefs.putBytes("hourly", hourly, sizeof(hourly_entry_t) * hourly_count);
    prefs.putBytes("daily", daily, sizeof(daily_entry_t) * daily_count);
    prefs.putChar("hcnt", hourly_count);
    prefs.putChar("dcnt", daily_count);
    prefs.putBool("valid", true);
    prefs.putULong("ftime", millis());
    prefs.end();
}

static void load_cache()
{
    Preferences prefs;
    prefs.begin("weather", true);
    if (prefs.getBool("valid", false)) {
        if (prefs.getBytes("cur", &cur, sizeof(cur)) == sizeof(cur)) {
            String loc = prefs.getString("locname", "");
            strncpy(location_name, loc.c_str(), 63);
            hourly_count = prefs.getChar("hcnt", 0);
            daily_count = prefs.getChar("dcnt", 0);
            if (hourly_count > MAX_HOURLY) hourly_count = MAX_HOURLY;
            if (daily_count > MAX_DAILY) daily_count = MAX_DAILY;
            prefs.getBytes("hourly", hourly, sizeof(hourly_entry_t) * hourly_count);
            prefs.getBytes("daily", daily, sizeof(daily_entry_t) * daily_count);
            last_fetch_time = prefs.getULong("ftime", 0);
            data_valid = true;
        }
    }
    prefs.end();
}

static bool cache_is_fresh()
{
    if (!data_valid || last_fetch_time == 0) return false;
    uint32_t now = millis();
    if (now < last_fetch_time) return false;
    return (now - last_fetch_time) < 3600000UL;
}

static void weather_fetch_task(void *param)
{
#ifdef OWM_API_KEY
    char url[256];

    float lat = 37.49f;
    float lon = -122.27f;
    const char *loc_source = "fallback";

    gps_params_t gps;
    if (hw_get_gps_info(gps) && gps.lat != 0 && gps.lng != 0) {
        lat = gps.lat;
        lon = gps.lng;
        loc_source = "GPS";
        Preferences prefs;
        prefs.begin("weather", false);
        prefs.putFloat("gps_lat", lat);
        prefs.putFloat("gps_lon", lon);
        prefs.end();
    } else {
        Preferences prefs;
        prefs.begin("weather", true);
        float cached_lat = prefs.getFloat("gps_lat", 0);
        float cached_lon = prefs.getFloat("gps_lon", 0);
        prefs.end();
        if (cached_lat != 0 && cached_lon != 0) {
            lat = cached_lat;
            lon = cached_lon;
            loc_source = "cached GPS";
        }
    }

    printf("[Weather] Using %s: lat=%.4f lon=%.4f\n", loc_source, lat, lon);

    snprintf(url, sizeof(url),
             "https://api.openweathermap.org/data/3.0/onecall?lat=%.4f&lon=%.4f&exclude=minutely,alerts&units=metric&appid=%s",
             lat, lon, OWM_API_KEY);

    printf("[Weather] Fetching One Call...\n");
    http_response_t resp = http_get(url, 15000);
    printf("[Weather] OneCall: status=%d success=%d body_len=%d\n",
           resp.status_code, resp.success, (int)resp.body.length());

    if (resp.success) {
        parse_onecall(resp.body.c_str());
        last_fetch_time = millis();
    } else {
        printf("[Weather] Error: %s\n", resp.body.c_str());
    }

    if (data_valid && location_name[0] == '\0') {
        fetch_city_name(lat, lon);
    }

    if (data_valid) {
        save_cache();
    }
#else
    (void)param;
#endif

    fetch_task = NULL;
    vTaskDelete(NULL);
}

#endif // ARDUINO

static void start_fetch()
{
#ifdef ARDUINO
#ifdef OWM_API_KEY
    if (!hw_get_wifi_connected()) {
        if (status_label) lv_label_set_text(status_label, "WiFi not connected");
        return;
    }
    if (fetch_task) return;
    if (cache_is_fresh()) {
        printf("[Weather] Cache fresh, skipping fetch\n");
        return;
    }
    if (status_label) lv_label_set_text(status_label, "Fetching...");
    xTaskCreatePinnedToCore(weather_fetch_task, "weather", 16384, NULL, 5, &fetch_task, 0);
#else
    if (status_label) lv_label_set_text(status_label, "No API key.\nDefine OWM_API_KEY in config_keys.h");
#endif
#endif
}

static void refresh_cb(lv_timer_t *t)
{
    if (data_valid && !fetch_task) {
        update_ui();
    }
    static int tick = 0;
    tick++;
    if (tick >= 600) {
        tick = 0;
        start_fetch();
    }
}

// --- Menu back handler ---

static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_btn_is_root(menu, obj)) {
        if (refresh_timer) { lv_timer_del(refresh_timer); refresh_timer = NULL; }
#ifdef ARDUINO
        if (fetch_task) { vTaskDelete(fetch_task); fetch_task = NULL; }
        hw_set_keyboard_read_callback(NULL);
#endif

        lv_obj_clean(menu);
        lv_obj_del(menu);
        menu = NULL;
        main_page = NULL;
        forecast_ui_built = false;
        city_label = NULL;
        updated_label = NULL;
        temp_label = NULL;
        detail_label = NULL;
        desc_label = NULL;
        status_label = NULL;
        weather_icon = NULL;
        if (quit_btn) { lv_obj_del_async(quit_btn); quit_btn = NULL; }
        menu_show();
    }
}

// --- Main entry ---

void ui_weather_enter(lv_obj_t *parent)
{
    menu = create_menu(parent, back_event_handler);
    forecast_ui_built = false;

    // Ensure keyboard indev is enabled (may have been disabled by keyboard app)
    enable_keyboard();

    lv_group_t *g = lv_group_get_default();

    // Single scrollable page - all content here
    main_page = lv_menu_page_create(menu, NULL);

    // --- Current weather section ---
    lv_obj_t *cur_cont = lv_menu_cont_create(main_page);
    lv_obj_set_flex_flow(cur_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cur_cont, 4, LV_PART_MAIN);

    // City + update time row
    lv_obj_t *top_row = lv_obj_create(cur_cont);
    lv_obj_set_size(top_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(top_row, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(top_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(top_row, 0, LV_PART_MAIN);
    lv_obj_remove_flag(top_row, LV_OBJ_FLAG_SCROLLABLE);

    city_label = lv_label_create(top_row);
    lv_label_set_text(city_label, "Loading...");
    lv_obj_align(city_label, LV_ALIGN_LEFT_MID, 0, 0);

    updated_label = lv_label_create(top_row);
    lv_label_set_text(updated_label, "");
    lv_obj_align(updated_label, LV_ALIGN_RIGHT_MID, 0, 0);

    // Icon + temperature row
    lv_obj_t *temp_row = lv_obj_create(cur_cont);
    lv_obj_set_size(temp_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(temp_row, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(temp_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(temp_row, 0, LV_PART_MAIN);
    lv_obj_remove_flag(temp_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(temp_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(temp_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(temp_row, 8, LV_PART_MAIN);

    weather_icon = lv_image_create(temp_row);
    lv_image_set_src(weather_icon, &img_w_cloud);

    temp_label = lv_label_create(temp_row);
    lv_obj_set_style_text_font(temp_label, &lv_font_montserrat_22, LV_PART_MAIN);
    lv_obj_set_style_text_color(temp_label, lv_color_make(255, 200, 50), LV_PART_MAIN);
    lv_label_set_text(temp_label, "-- C");

    desc_label = lv_label_create(cur_cont);
    lv_obj_set_style_text_color(desc_label, lv_color_make(180, 220, 255), LV_PART_MAIN);
    lv_label_set_text(desc_label, "");

    detail_label = lv_label_create(cur_cont);
    lv_label_set_text(detail_label, "");
    lv_obj_set_width(detail_label, lv_pct(100));

    status_label = lv_label_create(cur_cont);
    lv_obj_set_width(status_label, lv_pct(100));
    lv_obj_set_style_text_color(status_label, lv_color_make(200, 200, 200), LV_PART_MAIN);
    lv_label_set_text(status_label, "");

    // Add cur_cont to group for encoder navigation
    if (g) lv_group_add_obj(g, cur_cont);

    // Forecast section headers are added to group by build_forecast_ui()
    // when data arrives. Encoder navigates between 3 sections (cur, hourly, daily).

    lv_menu_set_page(menu, main_page);

#ifdef ARDUINO
    // Register keyboard callback for Space+key scroll shortcuts
    hw_set_keyboard_read_callback(weather_keyboard_cb);

    load_cache();
    if (data_valid) update_ui();
#endif

    start_fetch();
    refresh_timer = lv_timer_create(refresh_cb, 1000, NULL);

#ifdef USING_TOUCHPAD
    quit_btn = create_floating_button([](lv_event_t *e) {
        lv_obj_send_event(lv_menu_get_main_header_back_button(menu), LV_EVENT_CLICKED, NULL);
    }, NULL);
#endif
}

void ui_weather_exit(lv_obj_t *parent) {}

app_t ui_weather_main = {
    .setup_func_cb = ui_weather_enter,
    .exit_func_cb = ui_weather_exit,
    .user_data = nullptr,
};
