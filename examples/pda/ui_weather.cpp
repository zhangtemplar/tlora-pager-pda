/**
 * @file      ui_weather.cpp
 * @author    LilyGo
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-01
 * @brief     Weather app using OpenWeatherMap API with NVS caching.
 */
#include "ui_define.h"
#include "config_keys.h"
#include "http_utils.h"

#ifdef ARDUINO
#include <cJSON.h>
#include <Preferences.h>
#endif

static lv_obj_t *menu = NULL;
static lv_obj_t *quit_btn = NULL;
static lv_timer_t *refresh_timer = NULL;
static TaskHandle_t fetch_task = NULL;

// UI elements
static lv_obj_t *city_label = NULL;
static lv_obj_t *updated_label = NULL;
static lv_obj_t *temp_label = NULL;
static lv_obj_t *detail_label = NULL;
static lv_obj_t *icon_label = NULL;
static lv_obj_t *forecast_label = NULL;
static lv_obj_t *status_label = NULL;

// Cached weather data
static char cached_city[64] = "";
static float cached_temp = 0;
static int cached_humidity = 0;
static float cached_wind = 0;
static char cached_desc[64] = "";
static char cached_icon[8] = "";
static char cached_forecast[256] = "";
static bool data_valid = false;

static const char *weather_icon_symbol(const char *icon_code)
{
    if (!icon_code || !icon_code[0]) return LV_SYMBOL_CHARGE;
    char c0 = icon_code[0], c1 = icon_code[1];
    if (c0 == '0' && c1 == '1') return LV_SYMBOL_IMAGE;       // clear sky -> sun-like
    if (c0 == '0' && c1 == '2') return LV_SYMBOL_IMAGE;       // few clouds
    if (c0 == '0' && (c1 == '3' || c1 == '4')) return LV_SYMBOL_WIFI; // clouds
    if (c0 == '0' && c1 == '9') return LV_SYMBOL_DOWN;        // rain
    if (c0 == '1' && c1 == '0') return LV_SYMBOL_DOWN;        // rain
    if (c0 == '1' && c1 == '1') return LV_SYMBOL_CHARGE;      // thunderstorm
    if (c0 == '1' && c1 == '3') return LV_SYMBOL_DOWN;        // snow
    if (c0 == '5' && c1 == '0') return LV_SYMBOL_EYE_OPEN;    // mist
    return LV_SYMBOL_CHARGE;
}

static void update_ui()
{
    if (!data_valid) return;

    lv_label_set_text(city_label, cached_city);

    struct tm timeinfo;
    hw_get_date_time(timeinfo);
    lv_label_set_text_fmt(updated_label, "Updated: %02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);

    lv_label_set_text_fmt(temp_label, "%.0f C", cached_temp);
    lv_label_set_text_fmt(detail_label, "Humidity: %d%%\nWind: %.1f m/s\n%s",
                          cached_humidity, cached_wind, cached_desc);
    lv_label_set_text(icon_label, weather_icon_symbol(cached_icon));
    lv_label_set_text(forecast_label, cached_forecast);
    lv_label_set_text(status_label, "");
}

#ifdef ARDUINO

static void parse_current_weather(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return;

    cJSON *name = cJSON_GetObjectItem(root, "name");
    if (name && name->valuestring) strncpy(cached_city, name->valuestring, 63);

    cJSON *main_obj = cJSON_GetObjectItem(root, "main");
    if (main_obj) {
        cJSON *temp = cJSON_GetObjectItem(main_obj, "temp");
        if (temp) cached_temp = temp->valuedouble;
        cJSON *hum = cJSON_GetObjectItem(main_obj, "humidity");
        if (hum) cached_humidity = hum->valueint;
    }

    cJSON *wind_obj = cJSON_GetObjectItem(root, "wind");
    if (wind_obj) {
        cJSON *speed = cJSON_GetObjectItem(wind_obj, "speed");
        if (speed) cached_wind = speed->valuedouble;
    }

    cJSON *weather_arr = cJSON_GetObjectItem(root, "weather");
    if (weather_arr && cJSON_GetArraySize(weather_arr) > 0) {
        cJSON *w0 = cJSON_GetArrayItem(weather_arr, 0);
        cJSON *desc = cJSON_GetObjectItem(w0, "description");
        if (desc && desc->valuestring) strncpy(cached_desc, desc->valuestring, 63);
        cJSON *ic = cJSON_GetObjectItem(w0, "icon");
        if (ic && ic->valuestring) strncpy(cached_icon, ic->valuestring, 7);
    }

    data_valid = true;
    cJSON_Delete(root);
}

static void parse_forecast(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return;

    cJSON *list = cJSON_GetObjectItem(root, "list");
    if (!list) { cJSON_Delete(root); return; }

    const char *day_names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    cached_forecast[0] = '\0';

    int entries = 0;
    int prev_day = -1;
    int count = cJSON_GetArraySize(list);

    for (int i = 0; i < count && entries < 5; i++) {
        cJSON *item = cJSON_GetArrayItem(list, i);
        cJSON *dt = cJSON_GetObjectItem(item, "dt");
        if (!dt) continue;

        time_t ts = (time_t)dt->valueint;
        struct tm *tm_info = localtime(&ts);
        if (!tm_info || tm_info->tm_mday == prev_day) continue;
        prev_day = tm_info->tm_mday;

        cJSON *main_obj = cJSON_GetObjectItem(item, "main");
        if (!main_obj) continue;

        cJSON *temp_min = cJSON_GetObjectItem(main_obj, "temp_min");
        cJSON *temp_max = cJSON_GetObjectItem(main_obj, "temp_max");

        char line[48];
        snprintf(line, sizeof(line), "%s %.0f/%.0f  ",
                 day_names[tm_info->tm_wday],
                 temp_min ? temp_min->valuedouble : 0,
                 temp_max ? temp_max->valuedouble : 0);
        strncat(cached_forecast, line, sizeof(cached_forecast) - strlen(cached_forecast) - 1);
        entries++;
    }

    cJSON_Delete(root);
}

static void save_cache()
{
    Preferences prefs;
    prefs.begin("weather", false);
    prefs.putString("city", cached_city);
    prefs.putFloat("temp", cached_temp);
    prefs.putInt("humidity", cached_humidity);
    prefs.putFloat("wind", cached_wind);
    prefs.putString("desc", cached_desc);
    prefs.putString("icon", cached_icon);
    prefs.putString("forecast", cached_forecast);
    prefs.putBool("valid", true);
    prefs.end();
}

static void load_cache()
{
    Preferences prefs;
    prefs.begin("weather", true);
    if (prefs.getBool("valid", false)) {
        String c = prefs.getString("city", "");
        strncpy(cached_city, c.c_str(), 63);
        cached_temp = prefs.getFloat("temp", 0);
        cached_humidity = prefs.getInt("humidity", 0);
        cached_wind = prefs.getFloat("wind", 0);
        String d = prefs.getString("desc", "");
        strncpy(cached_desc, d.c_str(), 63);
        String ic = prefs.getString("icon", "");
        strncpy(cached_icon, ic.c_str(), 7);
        String fc = prefs.getString("forecast", "");
        strncpy(cached_forecast, fc.c_str(), 255);
        data_valid = true;
    }
    prefs.end();
}

static void weather_fetch_task(void *param)
{
#ifdef OWM_API_KEY
    char url[256];

    // Determine location
    const char *city = NULL;
    gps_params_t gps;
    bool use_gps = false;
    if (hw_get_gps_info(gps) && gps.lat != 0 && gps.lng != 0) {
        use_gps = true;
    }

#ifdef WEATHER_DEFAULT_CITY
    if (!use_gps) city = WEATHER_DEFAULT_CITY;
#endif

    if (!use_gps && !city) {
        city = "Shenzhen";
    }

    // Current weather
    if (use_gps) {
        snprintf(url, sizeof(url),
                 "https://api.openweathermap.org/data/2.5/weather?lat=%.4f&lon=%.4f&units=metric&appid=%s",
                 gps.lat, gps.lng, OWM_API_KEY);
    } else {
        snprintf(url, sizeof(url),
                 "https://api.openweathermap.org/data/2.5/weather?q=%s&units=metric&appid=%s",
                 city, OWM_API_KEY);
    }

    http_response_t resp = http_get(url, 10000);
    if (resp.success) {
        parse_current_weather(resp.body.c_str());
    }

    // 5-day forecast
    if (use_gps) {
        snprintf(url, sizeof(url),
                 "https://api.openweathermap.org/data/2.5/forecast?lat=%.4f&lon=%.4f&units=metric&appid=%s",
                 gps.lat, gps.lng, OWM_API_KEY);
    } else {
        snprintf(url, sizeof(url),
                 "https://api.openweathermap.org/data/2.5/forecast?q=%s&units=metric&appid=%s",
                 city, OWM_API_KEY);
    }

    resp = http_get(url, 10000);
    if (resp.success) {
        parse_forecast(resp.body.c_str());
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
        lv_label_set_text(status_label, "WiFi not connected");
        return;
    }
    if (fetch_task) return;
    lv_label_set_text(status_label, "Fetching...");
    xTaskCreatePinnedToCore(weather_fetch_task, "weather", 8192, NULL, 5, &fetch_task, 0);
#else
    lv_label_set_text(status_label, "No API key set.\nDefine OWM_API_KEY in config_keys.h");
#endif
#endif
}

static void refresh_cb(lv_timer_t *t)
{
    if (data_valid && !fetch_task) {
        update_ui();
    }
    // Auto-refresh every 30 min
    static int tick = 0;
    tick++;
    if (tick >= 1800) { // timer fires every 1s
        tick = 0;
        start_fetch();
    }
}

static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_btn_is_root(menu, obj)) {
        if (refresh_timer) { lv_timer_del(refresh_timer); refresh_timer = NULL; }
#ifdef ARDUINO
        if (fetch_task) { vTaskDelete(fetch_task); fetch_task = NULL; }
#endif
        lv_obj_clean(menu);
        lv_obj_del(menu);
        if (quit_btn) { lv_obj_del_async(quit_btn); quit_btn = NULL; }
        menu_show();
    }
}

void ui_weather_enter(lv_obj_t *parent)
{
    menu = create_menu(parent, back_event_handler);
    lv_obj_t *main_page = lv_menu_page_create(menu, NULL);

    lv_obj_t *cont = lv_obj_create(main_page);
    lv_obj_set_size(cont, lv_pct(100), lv_pct(100));
    lv_obj_set_style_border_width(cont, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont, 5, LV_PART_MAIN);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);

    // Top row: city + updated time
    lv_obj_t *top_row = lv_obj_create(cont);
    lv_obj_set_size(top_row, lv_pct(100), 25);
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

    // Current weather row
    lv_obj_t *weather_row = lv_obj_create(cont);
    lv_obj_set_size(weather_row, lv_pct(100), 70);
    lv_obj_set_style_border_width(weather_row, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(weather_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(weather_row, 0, LV_PART_MAIN);
    lv_obj_remove_flag(weather_row, LV_OBJ_FLAG_SCROLLABLE);

    icon_label = lv_label_create(weather_row);
    lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_label_set_text(icon_label, LV_SYMBOL_CHARGE);
    lv_obj_align(icon_label, LV_ALIGN_LEFT_MID, 5, 0);

    temp_label = lv_label_create(weather_row);
    lv_obj_set_style_text_font(temp_label, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(temp_label, lv_color_make(255, 200, 50), LV_PART_MAIN);
    lv_label_set_text(temp_label, "-- C");
    lv_obj_align(temp_label, LV_ALIGN_LEFT_MID, 50, 0);

    detail_label = lv_label_create(weather_row);
    lv_label_set_text(detail_label, "");
    lv_obj_align(detail_label, LV_ALIGN_RIGHT_MID, -5, 0);

    // Forecast row
    forecast_label = lv_label_create(cont);
    lv_obj_set_width(forecast_label, lv_pct(100));
    lv_label_set_long_mode(forecast_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(forecast_label, "Forecast: loading...");

    // Status
    status_label = lv_label_create(cont);
    lv_obj_set_width(status_label, lv_pct(100));
    lv_obj_set_style_text_color(status_label, lv_color_make(200, 200, 200), LV_PART_MAIN);
    lv_label_set_text(status_label, "");

    lv_menu_set_page(menu, main_page);

    // Load cached data first
#ifdef ARDUINO
    load_cache();
    if (data_valid) update_ui();
#endif

    // Start fetch
    start_fetch();

    // Refresh timer (1 second for UI update checks)
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
