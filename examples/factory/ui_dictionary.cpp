/**
 * @file      ui_dictionary.cpp
 * @author    LilyGo
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-01
 * @brief     Dictionary app with offline (SD) and online lookup.
 */
#include "ui_define.h"
#include "dict_lookup.h"
#include "http_utils.h"

static lv_obj_t *menu = NULL;
static lv_obj_t *quit_btn = NULL;
static lv_obj_t *search_ta = NULL;
static lv_obj_t *result_label = NULL;
static lv_obj_t *status_label = NULL;

static void do_search()
{
    const char *word = lv_textarea_get_text(search_ta);
    if (!word || word[0] == '\0') return;

    lv_label_set_text(status_label, "Searching...");

    dict_result_t result;
    bool found = false;

    // Try StarDict first (most common offline format)
    if (dict_stardict_available()) {
        found = dict_lookup_stardict(word, result);
    }

    // Try custom binary format
    if (!found && dict_offline_en_available()) {
        found = dict_lookup_offline_en(word, result);
    }

    // Try online if offline not found or not available
    if (!found) {
        found = dict_lookup_online(word, result);
    }

    if (found && result.found) {
        static char display_buf[2048];
        snprintf(display_buf, sizeof(display_buf),
                 "%s  %s  %s\n\n%s",
                 result.word.c_str(),
                 result.phonetic.c_str(),
                 result.part_of_speech.c_str(),
                 result.definition.c_str());
        lv_label_set_text(result_label, display_buf);
        lv_label_set_text(status_label, "");
    } else {
        lv_label_set_text(result_label, "Word not found.");
        lv_label_set_text(status_label, "Try connecting WiFi for online lookup.");
    }
}

static void ta_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;

    if (lv_indev_get_type(indev) == LV_INDEV_TYPE_ENCODER) {
        bool edited = lv_obj_has_state(ta, LV_STATE_EDITED);
        if (code == LV_EVENT_CLICKED) {
            if (edited) {
                lv_group_set_editing(lv_obj_get_group(ta), false);
                disable_keyboard();
            }
        } else if (code == LV_EVENT_FOCUSED) {
            if (edited) {
                enable_keyboard();
            }
        }
    }

    if (code == LV_EVENT_KEY) {
        lv_key_t key = *(lv_key_t *)lv_event_get_param(e);
        if (key == LV_KEY_ENTER) {
            lv_textarea_delete_char(ta);
            do_search();
            lv_event_stop_processing(e);
        }
    }

    if (lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD) {
        if (code == LV_EVENT_KEY) {
            lv_key_t key = *(lv_key_t *)lv_event_get_param(e);
            if (key == LV_KEY_ENTER) {
                lv_textarea_delete_char(ta);
                do_search();
                lv_event_stop_processing(e);
            }
        }
    }
}

static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_btn_is_root(menu, obj)) {
        disable_keyboard();
        lv_obj_clean(menu);
        lv_obj_del(menu);
        if (quit_btn) { lv_obj_del_async(quit_btn); quit_btn = NULL; }
        menu_show();
    }
}

void ui_dictionary_enter(lv_obj_t *parent)
{
    menu = create_menu(parent, back_event_handler);
    lv_obj_t *main_page = lv_menu_page_create(menu, NULL);

    lv_obj_t *cont = lv_obj_create(main_page);
    lv_obj_set_size(cont, lv_pct(100), lv_pct(100));
    lv_obj_set_style_border_width(cont, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont, 5, LV_PART_MAIN);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);

    // Search textarea
    search_ta = lv_textarea_create(cont);
    lv_obj_set_width(search_ta, lv_pct(100));
    lv_obj_set_height(search_ta, 40);
    lv_textarea_set_placeholder_text(search_ta, "Type a word and press Enter");
    lv_textarea_set_one_line(search_ta, true);
    lv_textarea_set_max_length(search_ta, 64);
    lv_obj_add_event_cb(search_ta, ta_event_cb, LV_EVENT_ALL, NULL);

    // Status label
    status_label = lv_label_create(cont);
    lv_obj_set_width(status_label, lv_pct(100));
    lv_obj_set_style_text_color(status_label, lv_color_make(180, 180, 180), LV_PART_MAIN);
    if (dict_stardict_available()) {
        lv_label_set_text(status_label, "StarDict available (SD card)");
    } else if (dict_offline_en_available()) {
        lv_label_set_text(status_label, "Offline dict available");
    } else {
        lv_label_set_text(status_label, "Online only (WiFi required)");
    }

    // Result label (scrollable)
    result_label = lv_label_create(cont);
    lv_obj_set_width(result_label, lv_pct(100));
    lv_label_set_long_mode(result_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(result_label, "");

    lv_menu_set_page(menu, main_page);

    // Focus and enable keyboard
    lv_group_add_obj(lv_group_get_default(), search_ta);
    lv_group_focus_obj(search_ta);
    lv_obj_add_state(search_ta, (lv_state_t)(LV_STATE_FOCUSED | LV_STATE_EDITED));
    enable_keyboard();

#ifdef USING_TOUCHPAD
    quit_btn = create_floating_button([](lv_event_t *e) {
        lv_obj_send_event(lv_menu_get_main_header_back_button(menu), LV_EVENT_CLICKED, NULL);
    }, NULL);
#endif
}

void ui_dictionary_exit(lv_obj_t *parent) {}

app_t ui_dictionary_main = {
    .setup_func_cb = ui_dictionary_enter,
    .exit_func_cb = ui_dictionary_exit,
    .user_data = nullptr,
};
