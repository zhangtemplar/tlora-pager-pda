/**
 * @file      ui_gps.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 */
#include "ui_define.h"

#ifdef ARDUINO
#include <SD.h>
#endif

LV_IMG_DECLARE(img_compass);
LV_IMG_DECLARE(img_compass_needle);

// --- Track recording data ---
struct track_point_t {
    double lat, lng;
    float alt, speed;
    struct tm time;
};

#define TRACK_MAX_POINTS 2000

static std::vector<track_point_t> track_points;
static bool track_recording = false;
static uint32_t track_interval_ms = 10000;
static uint32_t track_last_record = 0;
static float track_total_dist = 0.0f;

// --- UI widgets ---
static lv_obj_t *menu = NULL;
static lv_timer_t *timer = NULL;
static lv_obj_t *quit_btn = NULL;

// Status section
static lv_obj_t *lbl_fix_status = NULL;
static lv_obj_t *lbl_satellites = NULL;
static lv_obj_t *lbl_hdop = NULL;

// Position section
static lv_obj_t *lbl_lat = NULL;
static lv_obj_t *lbl_lng = NULL;
static lv_obj_t *lbl_alt = NULL;
static lv_obj_t *lbl_speed = NULL;
static lv_obj_t *lbl_course = NULL;

// Compass section
#if defined(USING_BHI260_SENSOR) || defined(USING_MAG_QMC5883)
static lv_obj_t *img_needle = NULL;
static lv_obj_t *lbl_heading = NULL;
#endif

// Track section
static lv_obj_t *lbl_track_status = NULL;
static lv_obj_t *lbl_track_stats = NULL;
static lv_obj_t *btn_track_toggle = NULL;
static lv_obj_t *lbl_save_feedback = NULL;

// --- Haversine distance helper (float precision, uses HW FPU) ---
static float haversine_m(double lat1, double lon1, double lat2, double lon2)
{
    float dlat = (float)(lat2 - lat1) * (float)(M_PI / 180.0);
    float dlon = (float)(lon2 - lon1) * (float)(M_PI / 180.0);
    float rlat1 = (float)(lat1 * M_PI / 180.0);
    float rlat2 = (float)(lat2 * M_PI / 180.0);
    float a = sinf(dlat * 0.5f) * sinf(dlat * 0.5f) +
              cosf(rlat1) * cosf(rlat2) * sinf(dlon * 0.5f) * sinf(dlon * 0.5f);
    return 6371000.0f * 2.0f * atan2f(sqrtf(a), sqrtf(1.0f - a));
}

// --- GPX save to SD ---
#if defined(ARDUINO) && defined(HAS_SD_CARD_SOCKET)
static bool save_gpx_to_sd(const struct tm &gps_time)
{
    if (track_points.empty()) return false;
    if (!(hw_get_device_online() & HW_SD_ONLINE)) return false;

    char buf[128];
    snprintf(buf, sizeof(buf), "/gps_tracks/track_%04d%02d%02d_%02d%02d%02d.gpx",
             gps_time.tm_year + 1900, gps_time.tm_mon + 1, gps_time.tm_mday,
             gps_time.tm_hour, gps_time.tm_min, gps_time.tm_sec);

    instance.lockSPI();

    if (!SD.exists("/gps_tracks")) {
        SD.mkdir("/gps_tracks");
    }

    File f = SD.open(buf, FILE_WRITE);
    if (!f) {
        instance.unlockSPI();
        return false;
    }

    // Save filename for feedback before buf is reused
    char saved_name[64];
    strncpy(saved_name, buf, sizeof(saved_name));

    f.print("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<gpx version=\"1.1\" creator=\"LilyGo-PDA\">\n<trk>\n");

    snprintf(buf, sizeof(buf), "<name>Track %04d-%02d-%02d %02d:%02d:%02d</name>\n<trkseg>\n",
             gps_time.tm_year + 1900, gps_time.tm_mon + 1, gps_time.tm_mday,
             gps_time.tm_hour, gps_time.tm_min, gps_time.tm_sec);
    f.print(buf);

    for (size_t i = 0; i < track_points.size(); i++) {
        const track_point_t &pt = track_points[i];
        snprintf(buf, sizeof(buf),
                 "<trkpt lat=\"%.6f\" lon=\"%.6f\">"
                 "<ele>%.1f</ele>"
                 "<time>%04d-%02d-%02dT%02d:%02d:%02dZ</time>"
                 "<speed>%.1f</speed>"
                 "</trkpt>\n",
                 pt.lat, pt.lng, pt.alt,
                 pt.time.tm_year + 1900, pt.time.tm_mon + 1, pt.time.tm_mday,
                 pt.time.tm_hour, pt.time.tm_min, pt.time.tm_sec,
                 pt.speed);
        f.print(buf);
    }

    f.print("</trkseg>\n</trk>\n</gpx>\n");
    f.close();
    instance.unlockSPI();

    if (lbl_save_feedback) {
        lv_label_set_text_fmt(lbl_save_feedback, "Saved: %s", saved_name);
    }
    return true;
}
#endif

// --- Back handler ---
static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_btn_is_root(menu, obj)) {
        if (timer) {
            lv_timer_del(timer);
            timer = NULL;
        }

#if defined(USING_BHI260_SENSOR)
        hw_unregister_imu_process();
#endif
#if defined(USING_MAG_QMC5883)
        hw_mag_enable(false);
#endif

        hw_gps_detach_pps();
        track_recording = false;

        lv_obj_clean(menu);
        lv_obj_del(menu);

        if (quit_btn) {
            lv_obj_del_async(quit_btn);
            quit_btn = NULL;
        }

        menu = NULL;
        menu_show();
    }
}

// --- Timer callback ---
static void gps_timer_cb(lv_timer_t *t)
{
    static gps_params_t param;
    param.enable_debug = false;
    bool fix = hw_get_gps_info(param);

    // Status
    if (param.satellite == 0) {
        lv_label_set_text(lbl_fix_status, "No GPS");
        lv_obj_set_style_text_color(lbl_fix_status, lv_palette_main(LV_PALETTE_RED), 0);
    } else if (!fix) {
        lv_label_set_text(lbl_fix_status, "Searching...");
        lv_obj_set_style_text_color(lbl_fix_status, lv_palette_main(LV_PALETTE_YELLOW), 0);
    } else if (param.satellite < 4) {
        lv_label_set_text(lbl_fix_status, "2D Fix");
        lv_obj_set_style_text_color(lbl_fix_status, lv_palette_main(LV_PALETTE_LIGHT_GREEN), 0);
    } else {
        lv_label_set_text(lbl_fix_status, "3D Fix");
        lv_obj_set_style_text_color(lbl_fix_status, lv_palette_main(LV_PALETTE_GREEN), 0);
    }

    lv_label_set_text_fmt(lbl_satellites, "Sats: %u", param.satellite);

    if (param.hdop > 0.01f) {
        const char *q = param.hdop < 1.0f ? "Excellent" :
                        param.hdop < 2.0f ? "Good" :
                        param.hdop < 5.0f ? "Moderate" : "Poor";
        lv_label_set_text_fmt(lbl_hdop, "HDOP: %.1f (%s)", param.hdop, q);
    } else {
        lv_label_set_text(lbl_hdop, "HDOP: N/A");
    }

    // Position
    if (fix) {
        lv_label_set_text_fmt(lbl_lat, "Lat:  %.6f %c", fabs(param.lat), param.lat >= 0 ? 'N' : 'S');
        lv_label_set_text_fmt(lbl_lng, "Lng:  %.6f %c", fabs(param.lng), param.lng >= 0 ? 'E' : 'W');
        lv_label_set_text_fmt(lbl_alt, "Alt:  %.1f m", param.altitude);
        lv_label_set_text_fmt(lbl_speed, "Speed: %.2f km/h", param.speed);
        if (param.speed > 1.0)
            lv_label_set_text_fmt(lbl_course, "Course: %.1f", param.course);
        else
            lv_label_set_text(lbl_course, "Course: --");
    } else {
        lv_label_set_text(lbl_lat, "Lat:  --");
        lv_label_set_text(lbl_lng, "Lng:  --");
        lv_label_set_text(lbl_alt, "Alt:  --");
        lv_label_set_text(lbl_speed, "Speed: --");
        lv_label_set_text(lbl_course, "Course: --");
    }

    // Compass
#if defined(USING_MAG_QMC5883)
    {
        float heading = hw_mag_get_polar();
        if (img_needle)
            lv_img_set_angle(img_needle, (int16_t)(-heading * 10));
        if (lbl_heading)
            lv_label_set_text_fmt(lbl_heading, "Heading: %.0f", heading);
    }
#elif defined(USING_BHI260_SENSOR)
    {
        imu_params_t imu;
        hw_get_imu_params(imu);
        if (img_needle)
            lv_img_set_angle(img_needle, (int16_t)(-imu.heading * 10));
        if (lbl_heading)
            lv_label_set_text_fmt(lbl_heading, "Heading: %.0f", imu.heading);
    }
#endif

    // Track recording
    if (track_recording && fix) {
        uint32_t now = lv_tick_get();
        if ((now - track_last_record) >= track_interval_ms) {
            track_last_record = now;
            if (track_points.size() < TRACK_MAX_POINTS) {
                track_point_t pt;
                pt.lat = param.lat;
                pt.lng = param.lng;
                pt.alt = param.altitude;
                pt.speed = (float)param.speed;
                pt.time = param.datetime;
                if (!track_points.empty()) {
                    const track_point_t &prev = track_points.back();
                    track_total_dist += haversine_m(prev.lat, prev.lng, pt.lat, pt.lng);
                }
                track_points.push_back(pt);
            }
        }
    }

    // Track stats
    if (lbl_track_status) {
        if (track_recording)
            lv_label_set_text_fmt(lbl_track_status, "Recording (every %us)", (unsigned)(track_interval_ms / 1000));
        else
            lv_label_set_text(lbl_track_status, "Stopped");
    }
    if (lbl_track_stats) {
        lv_label_set_text_fmt(lbl_track_stats, "Points: %u | Dist: %.2f km",
                              (unsigned)track_points.size(), track_total_dist / 1000.0f);
    }
}

// --- Main entry ---
void ui_gps_enter(lv_obj_t *parent)
{
    track_points.clear();
    track_recording = false;
    track_last_record = 0;
    track_total_dist = 0.0f;

    menu = create_menu(parent, back_event_handler);
    lv_obj_t *main_page = lv_menu_page_create(menu, NULL);

    lv_obj_t *list = lv_list_create(main_page);
    lv_obj_set_size(list, lv_pct(100), lv_pct(100));
    lv_obj_center(list);

    // ====== STATUS SECTION ======
    lv_list_add_text(list, "STATUS");

    lv_obj_t *btn;
    btn = lv_list_add_btn(list, LV_SYMBOL_GPS, "Fix");
    lbl_fix_status = lv_label_create(btn);
    lv_label_set_text(lbl_fix_status, "No GPS");
    lv_obj_set_style_text_color(lbl_fix_status, lv_palette_main(LV_PALETTE_RED), 0);

    btn = lv_list_add_btn(list, LV_SYMBOL_GPS, "Satellites");
    lbl_satellites = lv_label_create(btn);
    lv_label_set_text(lbl_satellites, "Sats: 0");

    btn = lv_list_add_btn(list, LV_SYMBOL_GPS, "Precision");
    lbl_hdop = lv_label_create(btn);
    lv_label_set_text(lbl_hdop, "HDOP: N/A");

    // ====== POSITION SECTION ======
    lv_list_add_text(list, "POSITION");

    btn = lv_list_add_btn(list, LV_SYMBOL_GPS, "Latitude");
    lbl_lat = lv_label_create(btn);
    lv_label_set_text(lbl_lat, "--");

    btn = lv_list_add_btn(list, LV_SYMBOL_GPS, "Longitude");
    lbl_lng = lv_label_create(btn);
    lv_label_set_text(lbl_lng, "--");

    btn = lv_list_add_btn(list, LV_SYMBOL_GPS, "Altitude");
    lbl_alt = lv_label_create(btn);
    lv_label_set_text(lbl_alt, "--");

    btn = lv_list_add_btn(list, LV_SYMBOL_GPS, "Speed");
    lbl_speed = lv_label_create(btn);
    lv_label_set_text(lbl_speed, "--");

    btn = lv_list_add_btn(list, LV_SYMBOL_GPS, "Course");
    lbl_course = lv_label_create(btn);
    lv_label_set_text(lbl_course, "--");

    // ====== COMPASS SECTION ======
#if defined(USING_BHI260_SENSOR) || defined(USING_MAG_QMC5883)
    lv_list_add_text(list, "COMPASS");

    lv_obj_t *compass_item = lv_obj_create(list);
    lv_obj_set_size(compass_item, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(compass_item, 0, 0);
    lv_obj_set_style_pad_all(compass_item, 4, 0);
    lv_obj_set_flex_flow(compass_item, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(compass_item, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *compass_img_cont = lv_obj_create(compass_item);
    lv_obj_set_size(compass_img_cont, 84, 84);
    lv_obj_set_style_border_width(compass_img_cont, 0, 0);
    lv_obj_set_style_pad_all(compass_img_cont, 0, 0);
    lv_obj_set_style_bg_opa(compass_img_cont, LV_OPA_TRANSP, 0);

    lv_obj_t *dial = lv_img_create(compass_img_cont);
    lv_img_set_src(dial, &img_compass);
    lv_obj_align(dial, LV_ALIGN_CENTER, 0, 0);

    img_needle = lv_img_create(compass_img_cont);
    lv_img_set_src(img_needle, &img_compass_needle);
    lv_obj_align(img_needle, LV_ALIGN_CENTER, 0, 0);
    lv_img_set_pivot(img_needle, 24, 24);
    lv_img_set_angle(img_needle, 0);

    lbl_heading = lv_label_create(compass_item);
    lv_label_set_text(lbl_heading, "Heading: --");
#endif

    // ====== TRACK SECTION ======
    lv_list_add_text(list, "TRACK");

    btn = lv_list_add_btn(list, LV_SYMBOL_LOOP, "Status");
    lbl_track_status = lv_label_create(btn);
    lv_label_set_text(lbl_track_status, "Stopped");

    btn = lv_list_add_btn(list, LV_SYMBOL_LIST, "Stats");
    lbl_track_stats = lv_label_create(btn);
    lv_label_set_text(lbl_track_stats, "0 pts | 0.00 km");

    // Interval dropdown
    btn = lv_list_add_btn(list, LV_SYMBOL_SETTINGS, "Interval");
    lv_obj_t *dd = lv_dropdown_create(btn);
    lv_dropdown_set_options(dd, "1s\n5s\n10s\n30s\n60s");
    lv_dropdown_set_selected(dd, 2);
    lv_obj_add_event_cb(dd, [](lv_event_t *e) {
        uint16_t sel = lv_dropdown_get_selected((lv_obj_t *)lv_event_get_target(e));
        const uint32_t vals[] = {1000, 5000, 10000, 30000, 60000};
        if (sel < 5) track_interval_ms = vals[sel];
    }, LV_EVENT_VALUE_CHANGED, NULL);

    // Start/Stop button
    btn = lv_list_add_btn(list, LV_SYMBOL_PLAY, "Start Recording");
    btn_track_toggle = btn;
    lv_obj_add_event_cb(btn, [](lv_event_t *e) {
        track_recording = !track_recording;
        if (track_recording) track_last_record = 0;
        lv_obj_t *b = (lv_obj_t *)lv_event_get_target(e);
        // Update button text by finding label child (index 1, after icon)
        lv_obj_t *lbl = lv_obj_get_child(b, 1);
        if (lbl) lv_label_set_text(lbl, track_recording ? "Stop Recording" : "Start Recording");
    }, LV_EVENT_CLICKED, NULL);

    // Clear button
    btn = lv_list_add_btn(list, LV_SYMBOL_TRASH, "Clear Track");
    lv_obj_add_event_cb(btn, [](lv_event_t *e) {
        track_points.clear();
        track_total_dist = 0.0f;
        track_recording = false;
        if (btn_track_toggle) {
            lv_obj_t *lbl = lv_obj_get_child(btn_track_toggle, 1);
            if (lbl) lv_label_set_text(lbl, "Start Recording");
        }
        if (lbl_save_feedback) lv_label_set_text(lbl_save_feedback, "Track cleared");
    }, LV_EVENT_CLICKED, NULL);

    // Save GPX button
#if defined(ARDUINO) && defined(HAS_SD_CARD_SOCKET)
    btn = lv_list_add_btn(list, LV_SYMBOL_SAVE, "Save GPX to SD");
    lv_obj_add_event_cb(btn, [](lv_event_t *e) {
        if (track_points.empty()) {
            if (lbl_save_feedback) lv_label_set_text(lbl_save_feedback, "No points to save");
            return;
        }
        struct tm st = track_points.back().time;
        if (!save_gpx_to_sd(st)) {
            if (lbl_save_feedback) lv_label_set_text(lbl_save_feedback, "Save failed (SD?)");
        }
    }, LV_EVENT_CLICKED, NULL);
#endif

    btn = lv_list_add_btn(list, LV_SYMBOL_FILE, "");
    lbl_save_feedback = lv_obj_get_child(btn, 1);

    lv_menu_set_page(menu, main_page);

    // Init hardware
    hw_gps_attach_pps();

#if defined(USING_BHI260_SENSOR)
    hw_register_imu_process();
#endif
#if defined(USING_MAG_QMC5883)
    hw_mag_enable(true);
#endif

    timer = lv_timer_create(gps_timer_cb, 1000, NULL);

#ifdef USING_TOUCHPAD
    quit_btn = create_floating_button([](lv_event_t *e) {
        lv_obj_send_event(lv_menu_get_main_header_back_button(menu), LV_EVENT_CLICKED, NULL);
    }, NULL);
#endif
}

void ui_gps_exit(lv_obj_t *parent)
{
}

app_t ui_gps_main = {
    .setup_func_cb = ui_gps_enter,
    .exit_func_cb = ui_gps_exit,
    .user_data = nullptr,
};
