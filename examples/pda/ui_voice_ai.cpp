/**
 * @file      ui_voice_ai.cpp
 * @author    LilyGo
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-01
 * @brief     Voice AI app using Google Gemini API with audio recording.
 */
#include "ui_define.h"
#include "config_keys.h"
#include "gemini_api.h"
#include "http_utils.h"

static lv_obj_t *menu = NULL;
static lv_obj_t *quit_btn = NULL;
static lv_obj_t *response_label = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *input_ta = NULL;
static TaskHandle_t ai_task = NULL;
static bool is_recording = false;

#ifdef ARDUINO
#include <esp_heap_caps.h>

static uint8_t *wav_buffer = NULL;
static size_t wav_size = 0;

static void ai_audio_task(void *param)
{
#ifdef GEMINI_API_KEY
    lv_label_set_text(status_label, "Processing...");

    gemini_response_t resp = gemini_send_audio(wav_buffer, wav_size, GEMINI_API_KEY);

    // Free WAV buffer
    if (wav_buffer) { free(wav_buffer); wav_buffer = NULL; wav_size = 0; }

    if (resp.success) {
        lv_label_set_text(response_label, resp.text.c_str());
        lv_label_set_text(status_label, "Hold Space to record, or type below");
    } else {
        lv_label_set_text_fmt(response_label, "Error: %s", resp.error.c_str());
        lv_label_set_text(status_label, "Try again");
    }
#else
    lv_label_set_text(status_label, "No API key. Define GEMINI_API_KEY in config_keys.h");
#endif

    ai_task = NULL;
    vTaskDelete(NULL);
}

static void ai_text_task(void *param)
{
    char *prompt = (char *)param;
#ifdef GEMINI_API_KEY
    lv_label_set_text(status_label, "Thinking...");

    gemini_response_t resp = gemini_send_text(prompt, GEMINI_API_KEY);

    if (resp.success) {
        lv_label_set_text(response_label, resp.text.c_str());
        lv_label_set_text(status_label, "Hold Space to record, or type below");
    } else {
        lv_label_set_text_fmt(response_label, "Error: %s", resp.error.c_str());
        lv_label_set_text(status_label, "Try again");
    }
#else
    lv_label_set_text(status_label, "No API key. Define GEMINI_API_KEY in config_keys.h");
#endif

    free(prompt);
    ai_task = NULL;
    vTaskDelete(NULL);
}

static void start_recording()
{
    if (is_recording || ai_task) return;
    is_recording = true;
    lv_label_set_text(status_label, "Recording... release Space to stop");
    lv_label_set_text(response_label, "");
}

static void stop_recording()
{
    if (!is_recording) return;
    is_recording = false;
    lv_label_set_text(status_label, "Processing...");

    // Record 5 seconds of audio using the codec
    wav_buffer = NULL;
    wav_size = 0;

    lv_label_set_text(status_label, "Recording 5 sec...");

    // Note: The actual recording would use:
    // instance.codec.recordWAV(5, &wav_buffer, &wav_size, 16000, 1);
    // Since we can't call instance directly from UI code, we'd use a HAL function.
    // For now, show an error if recording isn't available.
    lv_label_set_text(status_label, "Audio recording not yet integrated.\nUse text input below.");
}
#endif

static void keyboard_read_cb(int state, char &c)
{
#ifdef ARDUINO
    if (c == ' ') {
        if (state == 1) {  // key pressed
            start_recording();
        } else if (state == 0) {  // key released
            stop_recording();
        }
        c = 0;  // consume the key
    }
#endif
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
            const char *text = lv_textarea_get_text(ta);
            if (text && text[0] != '\0' && !ai_task) {
#ifdef ARDUINO
                char *prompt = strdup(text);
                if (prompt) {
                    xTaskCreatePinnedToCore(ai_text_task, "ai_text", 8192, prompt, 5, &ai_task, 0);
                }
#endif
                lv_textarea_set_text(ta, "");
            }
            lv_event_stop_processing(e);
        }
    }

    if (lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD) {
        if (code == LV_EVENT_KEY) {
            lv_key_t key = *(lv_key_t *)lv_event_get_param(e);
            if (key == LV_KEY_ENTER) {
                lv_textarea_delete_char(ta);
                const char *text = lv_textarea_get_text(ta);
                if (text && text[0] != '\0' && !ai_task) {
#ifdef ARDUINO
                    char *prompt = strdup(text);
                    if (prompt) {
                        xTaskCreatePinnedToCore(ai_text_task, "ai_text", 8192, prompt, 5, &ai_task, 0);
                    }
#endif
                    lv_textarea_set_text(ta, "");
                }
                lv_event_stop_processing(e);
            }
        }
    }
}

static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_btn_is_root(menu, obj)) {
#ifdef ARDUINO
        if (ai_task) { vTaskDelete(ai_task); ai_task = NULL; }
        if (wav_buffer) { free(wav_buffer); wav_buffer = NULL; }
        hw_set_keyboard_read_callback(NULL);
#endif
        disable_keyboard();
        lv_obj_clean(menu);
        lv_obj_del(menu);
        if (quit_btn) { lv_obj_del_async(quit_btn); quit_btn = NULL; }
        menu_show();
    }
}

void ui_voice_ai_enter(lv_obj_t *parent)
{
    menu = create_menu(parent, back_event_handler);
    lv_obj_t *main_page = lv_menu_page_create(menu, NULL);

    lv_obj_t *cont = lv_obj_create(main_page);
    lv_obj_set_size(cont, lv_pct(100), lv_pct(100));
    lv_obj_set_style_border_width(cont, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont, 5, LV_PART_MAIN);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);

    // Status label
    status_label = lv_label_create(cont);
    lv_obj_set_width(status_label, lv_pct(100));
    lv_obj_set_style_text_color(status_label, lv_color_make(100, 200, 255), LV_PART_MAIN);

#ifdef GEMINI_API_KEY
    if (!hw_get_wifi_connected()) {
        lv_label_set_text(status_label, "WiFi required. Connect first.");
    } else {
        lv_label_set_text(status_label, "Type a question below and press Enter");
    }
#else
    lv_label_set_text(status_label, "Set GEMINI_API_KEY in config_keys.h");
#endif

    // Response label (scrollable)
    response_label = lv_label_create(cont);
    lv_obj_set_width(response_label, lv_pct(100));
    lv_obj_set_flex_grow(response_label, 1);
    lv_label_set_long_mode(response_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(response_label, "");

    // Text input area
    input_ta = lv_textarea_create(cont);
    lv_obj_set_width(input_ta, lv_pct(100));
    lv_obj_set_height(input_ta, 40);
    lv_textarea_set_placeholder_text(input_ta, "Ask a question...");
    lv_textarea_set_one_line(input_ta, true);
    lv_textarea_set_max_length(input_ta, 256);
    lv_obj_add_event_cb(input_ta, ta_event_cb, LV_EVENT_ALL, NULL);

    lv_menu_set_page(menu, main_page);

    // Focus and enable keyboard
    lv_group_add_obj(lv_group_get_default(), input_ta);
    lv_group_focus_obj(input_ta);
    lv_obj_add_state(input_ta, (lv_state_t)(LV_STATE_FOCUSED | LV_STATE_EDITED));
    enable_keyboard();

#ifdef USING_TOUCHPAD
    quit_btn = create_floating_button([](lv_event_t *e) {
        lv_obj_send_event(lv_menu_get_main_header_back_button(menu), LV_EVENT_CLICKED, NULL);
    }, NULL);
#endif
}

void ui_voice_ai_exit(lv_obj_t *parent) {}

app_t ui_voice_ai_main = {
    .setup_func_cb = ui_voice_ai_enter,
    .exit_func_cb = ui_voice_ai_exit,
    .user_data = nullptr,
};
