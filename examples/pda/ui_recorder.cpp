/**
 * @file      ui_recorder.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-01
 *
 */
#include "ui_define.h"

#if defined(USING_AUDIO_CODEC) && defined(HAS_SD_CARD_SOCKET)
#define RECORDER_ENABLED
#endif

static lv_obj_t *menu = NULL;
static lv_obj_t *quit_btn = NULL;
static lv_timer_t *rec_timer = NULL;
static lv_obj_t *rec_btn = NULL;
static lv_obj_t *rec_btn_label = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *duration_label = NULL;
static lv_obj_t *rec_list = NULL;

static vector<string> rec_files;
static bool is_recording = false;

static void refresh_file_list();

static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_btn_is_root(menu, obj)) {
        if (is_recording) {
            hw_recorder_stop();
            is_recording = false;
        }
        hw_set_play_stop();

        if (rec_timer) {
            lv_timer_del(rec_timer);
            rec_timer = NULL;
        }

        rec_files.clear();
        lv_obj_clean(menu);
        lv_obj_del(menu);

        if (quit_btn) {
            lv_obj_del_async(quit_btn);
            quit_btn = NULL;
        }

        menu_show();
    }
}

static void generate_filename(char *buf, size_t len)
{
    struct tm timeinfo;
    hw_get_date_time(timeinfo);
    snprintf(buf, len, "recordings/rec_%04d%02d%02d_%02d%02d%02d.wav",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}

static void rec_btn_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) return;

    if (!is_recording) {
        char filename[64];
        generate_filename(filename, sizeof(filename));

        if (hw_recorder_start(filename)) {
            is_recording = true;
            lv_label_set_text(rec_btn_label, LV_SYMBOL_STOP);
            lv_label_set_text(status_label, "Recording...");
            lv_label_set_text(duration_label, "0:00");
        } else {
            lv_label_set_text(status_label, "Start failed");
        }
    } else {
        lv_label_set_text(status_label, "Saving...");
        lv_label_set_text(rec_btn_label, LV_SYMBOL_AUDIO);
        lv_refr_now(NULL);

        hw_recorder_stop();
        is_recording = false;

        lv_label_set_text(status_label, "Ready");
        refresh_file_list();
    }
}

static void play_btn_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) return;
    if (is_recording) return;

    char *filepath = (char *)lv_event_get_user_data(e);
    if (filepath) {
        hw_set_sd_music_play(AUDIO_SOURCE_SDCARD, filepath);
        lv_label_set_text(status_label, "Playing...");
    }
}

static void delete_btn_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) return;
    if (is_recording) return;

    char *filepath = (char *)lv_event_get_user_data(e);
    if (!filepath) return;

#if defined(ARDUINO) && defined(HAS_SD_CARD_SOCKET)
    String full = String("/") + String(filepath);
    instance.lockSPI();
    SD.remove(full);
    instance.unlockSPI();
#endif

    hw_set_play_stop();
    lv_label_set_text(status_label, "Deleted");
    refresh_file_list();
}

static void scan_recordings()
{
    rec_files.clear();
#if defined(ARDUINO) && defined(HAS_SD_CARD_SOCKET)
    instance.lockSPI();
    hw_sd_begin();
    File dir = SD.open("/recordings");
    if (dir && dir.isDirectory()) {
        File f = dir.openNextFile();
        while (f) {
            String name = f.name();
            if (name.endsWith(".wav")) {
                string path = "recordings/";
                path += name.c_str();
                rec_files.push_back(path);
            }
            f.close();
            f = dir.openNextFile();
        }
        dir.close();
    }
    instance.unlockSPI();
#endif
}

static void ensure_recordings_dir()
{
#if defined(ARDUINO) && defined(HAS_SD_CARD_SOCKET)
    instance.lockSPI();
    hw_sd_begin();
    if (!SD.exists("/recordings")) {
        SD.mkdir("/recordings");
    }
    instance.unlockSPI();
#endif
}

static void refresh_file_list()
{
    if (!rec_list) return;

    lv_obj_clean(rec_list);
    scan_recordings();

    if (rec_files.empty()) {
        lv_obj_t *label = lv_label_create(rec_list);
        lv_label_set_text(label, "No recordings");
        lv_obj_set_style_text_color(label, lv_palette_main(LV_PALETTE_GREY), 0);
        return;
    }

    for (size_t i = 0; i < rec_files.size(); i++) {
        // Extract just the filename for display
        const char *full_path = rec_files[i].c_str();
        const char *display_name = strrchr(full_path, '/');
        display_name = display_name ? display_name + 1 : full_path;

        lv_obj_t *btn = lv_list_add_button(rec_list, LV_SYMBOL_AUDIO, display_name);
        lv_obj_set_user_data(btn, (void *)rec_files[i].c_str());

        // Play button
        lv_obj_t *play_label = lv_label_create(btn);
        lv_label_set_text(play_label, LV_SYMBOL_PLAY);
        lv_obj_add_event_cb(btn, play_btn_event, LV_EVENT_CLICKED, (void *)rec_files[i].c_str());

        // Delete button
        lv_obj_t *del_btn = lv_btn_create(btn);
        lv_obj_set_size(del_btn, 30, 30);
        lv_obj_set_style_bg_color(del_btn, lv_palette_main(LV_PALETTE_RED), 0);
        lv_obj_set_style_pad_all(del_btn, 2, 0);
        lv_obj_t *del_label = lv_label_create(del_btn);
        lv_label_set_text(del_label, LV_SYMBOL_TRASH);
        lv_obj_center(del_label);
        lv_obj_add_event_cb(del_btn, delete_btn_event, LV_EVENT_CLICKED, (void *)rec_files[i].c_str());
    }
}

static void rec_timer_cb(lv_timer_t *t)
{
    if (is_recording) {
        uint32_t ms = hw_recorder_get_duration_ms();
        uint32_t secs = ms / 1000;
        uint32_t mins = secs / 60;
        secs = secs % 60;
        lv_label_set_text_fmt(duration_label, "%lu:%02lu", mins, secs);
    }

    if (!is_recording && status_label) {
        char *text = lv_label_get_text(status_label);
        if (strcmp(text, "Playing...") == 0 && !hw_player_running()) {
            lv_label_set_text(status_label, "Ready");
        }
    }
}

void ui_recorder_enter(lv_obj_t *parent)
{
    is_recording = false;

    menu = create_menu(parent, back_event_handler);

    lv_obj_t *main_page = lv_menu_page_create(menu, NULL);
    lv_obj_set_scrollbar_mode(main_page, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(main_page, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(main_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(main_page, 5, 0);

#ifndef RECORDER_ENABLED
    lv_obj_t *label = lv_label_create(main_page);
    lv_label_set_text(label, "Recorder requires\naudio codec & SD card");
    lv_obj_set_width(label, lv_pct(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_menu_set_page(menu, main_page);
    return;
#else
    // Check audio codec
    if (!(HW_CODEC_ONLINE & hw_get_device_online())) {
        lv_obj_t *label = lv_label_create(main_page);
        lv_label_set_text(label, "No audio hardware");
        lv_obj_set_width(label, lv_pct(100));
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_menu_set_page(menu, main_page);
        return;
    }

    // Check SD card
    Serial.println("[Recorder] Checking SD card on entry...");
    bool sd_ok = hw_mount_sd();
    Serial.printf("[Recorder] hw_mount_sd() returned: %d\n", sd_ok);
    if (!sd_ok) {
        Serial.println("[Recorder] SD card mount failed on entry");
        lv_obj_t *label = lv_label_create(main_page);
        lv_label_set_text(label, "No SD card detected");
        lv_obj_set_width(label, lv_pct(100));
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_menu_set_page(menu, main_page);
        return;
    }
    Serial.println("[Recorder] SD card OK");

    ensure_recordings_dir();

    // Control row
    lv_obj_t *ctrl_row = lv_obj_create(main_page);
    lv_obj_set_size(ctrl_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ctrl_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctrl_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_width(ctrl_row, 0, 0);
    lv_obj_set_style_pad_all(ctrl_row, 5, 0);
    lv_obj_remove_flag(ctrl_row, LV_OBJ_FLAG_SCROLLABLE);

    // Record button
    rec_btn = lv_btn_create(ctrl_row);
    lv_obj_set_size(rec_btn, 50, 40);
    lv_obj_set_style_bg_color(rec_btn, lv_palette_main(LV_PALETTE_RED), 0);
    rec_btn_label = lv_label_create(rec_btn);
    lv_label_set_text(rec_btn_label, LV_SYMBOL_AUDIO);
    lv_obj_center(rec_btn_label);
    lv_obj_add_event_cb(rec_btn, rec_btn_event, LV_EVENT_CLICKED, NULL);

    // Status + duration column
    lv_obj_t *info_col = lv_obj_create(ctrl_row);
    lv_obj_set_size(info_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(info_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_border_width(info_col, 0, 0);
    lv_obj_set_style_pad_all(info_col, 0, 0);
    lv_obj_remove_flag(info_col, LV_OBJ_FLAG_SCROLLABLE);

    status_label = lv_label_create(info_col);
    lv_label_set_text(status_label, "Ready");

    duration_label = lv_label_create(info_col);
    lv_label_set_text(duration_label, "0:00");

    // Recordings list
    lv_obj_t *list_header = lv_label_create(main_page);
    lv_label_set_text(list_header, "RECORDINGS");
    lv_obj_set_style_text_color(list_header, lv_palette_main(LV_PALETTE_GREY), 0);

    rec_list = lv_list_create(main_page);
    lv_obj_set_size(rec_list, lv_pct(100), lv_pct(65));
    lv_obj_set_style_border_width(rec_list, 0, LV_PART_MAIN);
    lv_obj_set_flex_grow(rec_list, 1);

    refresh_file_list();

    lv_menu_set_page(menu, main_page);

    rec_timer = lv_timer_create(rec_timer_cb, 500, NULL);

#ifdef USING_TOUCHPAD
    quit_btn = create_floating_button([](lv_event_t *e) {
        lv_obj_send_event(lv_menu_get_main_header_back_button(menu), LV_EVENT_CLICKED, NULL);
    }, NULL);
#endif

#endif // RECORDER_ENABLED
}

void ui_recorder_exit(lv_obj_t *parent)
{
}

app_t ui_recorder_main = {
    .setup_func_cb = ui_recorder_enter,
    .exit_func_cb = ui_recorder_exit,
    .user_data = nullptr,
};
