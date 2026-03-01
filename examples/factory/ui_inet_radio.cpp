/**
 * @file      ui_inet_radio.cpp
 * @author    LilyGo
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-01
 * @brief     Internet Radio app with station presets and buffered MP3 streaming.
 */
#include "ui_define.h"
#include "audio_stream.h"
#include "http_utils.h"

static lv_obj_t *menu = NULL;
static lv_obj_t *quit_btn = NULL;
static lv_timer_t *update_timer = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *buffer_label = NULL;
static lv_obj_t *vol_label = NULL;
static lv_obj_t *station_list = NULL;
static int selected_station = -1;
static uint8_t current_volume = 75;

static void update_status_cb(lv_timer_t *t)
{
    if (radio_stream_is_playing()) {
        int pct = radio_stream_buffer_pct();
        if (pct < 100) {
            lv_label_set_text_fmt(buffer_label, "Buffering... %d%%", pct);
        } else {
            lv_label_set_text(buffer_label, "");
        }
        lv_label_set_text(status_label, LV_SYMBOL_PLAY " Playing");
    } else {
        lv_label_set_text(buffer_label, "");
        if (selected_station >= 0) {
            lv_label_set_text(status_label, LV_SYMBOL_STOP " Stopped");
        } else {
            lv_label_set_text(status_label, "Select a station");
        }
    }
}

static void play_station(int idx)
{
    if (!http_require_wifi("Internet Radio")) return;

    int count;
    const radio_station_t *stations = radio_get_presets(&count);
    if (idx < 0 || idx >= count) return;

    selected_station = idx;
    lv_label_set_text_fmt(status_label, "Connecting to %s...", stations[idx].name);
    radio_stream_start(stations[idx].url);
}

static void stop_playback()
{
    radio_stream_stop();
    lv_label_set_text(status_label, LV_SYMBOL_STOP " Stopped");
}

static void station_click_cb(lv_event_t *e)
{
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_event_get_user_data(e);

    if (radio_stream_is_playing() && idx == selected_station) {
        stop_playback();
    } else {
        stop_playback();
        play_station(idx);
    }
}

static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_btn_is_root(menu, obj)) {
        if (update_timer) { lv_timer_del(update_timer); update_timer = NULL; }
        radio_stream_stop();
        lv_obj_clean(menu);
        lv_obj_del(menu);
        if (quit_btn) { lv_obj_del_async(quit_btn); quit_btn = NULL; }
        menu_show();
    }
}

void ui_inet_radio_enter(lv_obj_t *parent)
{
    menu = create_menu(parent, back_event_handler);
    lv_obj_t *main_page = lv_menu_page_create(menu, NULL);

    lv_obj_t *cont = lv_obj_create(main_page);
    lv_obj_set_size(cont, lv_pct(100), lv_pct(100));
    lv_obj_set_style_border_width(cont, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont, 5, LV_PART_MAIN);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);

    // Title row with volume
    lv_obj_t *title_row = lv_obj_create(cont);
    lv_obj_set_size(title_row, lv_pct(100), 30);
    lv_obj_set_style_border_width(title_row, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(title_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(title_row, 0, LV_PART_MAIN);
    lv_obj_remove_flag(title_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(title_row);
    lv_label_set_text(title, "Internet Radio");
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);

    // Volume controls
    lv_obj_t *vol_down = lv_btn_create(title_row);
    lv_obj_set_size(vol_down, 30, 25);
    lv_obj_align(vol_down, LV_ALIGN_RIGHT_MID, -80, 0);
    lv_obj_t *vd_lbl = lv_label_create(vol_down);
    lv_label_set_text(vd_lbl, LV_SYMBOL_MINUS);
    lv_obj_center(vd_lbl);
    lv_obj_add_event_cb(vol_down, [](lv_event_t *e) {
        if (current_volume >= 10) current_volume -= 10;
        radio_stream_set_volume(current_volume);
        lv_label_set_text_fmt(vol_label, "Vol: %d", current_volume);
    }, LV_EVENT_CLICKED, NULL);

    vol_label = lv_label_create(title_row);
    lv_label_set_text_fmt(vol_label, "Vol: %d", current_volume);
    lv_obj_align(vol_label, LV_ALIGN_RIGHT_MID, -38, 0);

    lv_obj_t *vol_up = lv_btn_create(title_row);
    lv_obj_set_size(vol_up, 30, 25);
    lv_obj_align(vol_up, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_t *vu_lbl = lv_label_create(vol_up);
    lv_label_set_text(vu_lbl, LV_SYMBOL_PLUS);
    lv_obj_center(vu_lbl);
    lv_obj_add_event_cb(vol_up, [](lv_event_t *e) {
        if (current_volume <= 90) current_volume += 10;
        radio_stream_set_volume(current_volume);
        lv_label_set_text_fmt(vol_label, "Vol: %d", current_volume);
    }, LV_EVENT_CLICKED, NULL);

    // Station list
    station_list = lv_list_create(cont);
    lv_obj_set_size(station_list, lv_pct(100), 100);

    int count;
    const radio_station_t *stations = radio_get_presets(&count);
    for (int i = 0; i < count; i++) {
        lv_obj_t *btn = lv_list_add_btn(station_list, LV_SYMBOL_AUDIO, stations[i].name);
        lv_obj_add_event_cb(btn, station_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }

    // Status & buffer
    status_label = lv_label_create(cont);
    lv_obj_set_width(status_label, lv_pct(100));
    lv_label_set_text(status_label, "Select a station");

    buffer_label = lv_label_create(cont);
    lv_obj_set_width(buffer_label, lv_pct(100));
    lv_obj_set_style_text_color(buffer_label, lv_color_make(200, 200, 100), LV_PART_MAIN);
    lv_label_set_text(buffer_label, "");

    // Stop button
    lv_obj_t *stop_btn = lv_btn_create(cont);
    lv_obj_set_size(stop_btn, 80, 35);
    lv_obj_t *stop_lbl = lv_label_create(stop_btn);
    lv_label_set_text(stop_lbl, LV_SYMBOL_STOP " Stop");
    lv_obj_center(stop_lbl);
    lv_obj_add_event_cb(stop_btn, [](lv_event_t *e) {
        stop_playback();
    }, LV_EVENT_CLICKED, NULL);

    lv_menu_set_page(menu, main_page);

    // Set volume
    radio_stream_set_volume(current_volume);

    // Status update timer
    update_timer = lv_timer_create(update_status_cb, 500, NULL);

#ifdef USING_TOUCHPAD
    quit_btn = create_floating_button([](lv_event_t *e) {
        lv_obj_send_event(lv_menu_get_main_header_back_button(menu), LV_EVENT_CLICKED, NULL);
    }, NULL);
#endif
}

void ui_inet_radio_exit(lv_obj_t *parent) {}

app_t ui_inet_radio_main = {
    .setup_func_cb = ui_inet_radio_enter,
    .exit_func_cb = ui_inet_radio_exit,
    .user_data = nullptr,
};
