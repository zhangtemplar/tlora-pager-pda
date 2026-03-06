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
static lv_obj_t *input_ta = NULL;
static TaskHandle_t ai_task = NULL;
static lv_obj_t *scroll_cont = NULL;
static char *chat_history = NULL;

#ifdef ARDUINO
#include <esp_heap_caps.h>
#include <freertos/queue.h>
#include <SD.h>

static uint8_t *wav_buffer = NULL;
static size_t wav_size = 0;
static lv_font_t *ttf_font = NULL;
static uint8_t *ttf_data = NULL;
static lv_font_t response_font;

// Thread-safe UI messaging: tasks post messages via queue,
// LVGL timer drains them on the main thread.
enum { UI_MSG_APPEND = 1, UI_MSG_STATUS = 2 };
struct ui_msg_t { int type; char *text; };
static QueueHandle_t ui_queue = NULL;
static lv_timer_t *ui_timer = NULL;

// Append text to chat_history (MUST be called on main LVGL thread).
static void chat_append(const char *text)
{
    if (!text || !text[0]) return;

    if (chat_history) {
        size_t old_len = strlen(chat_history);
        size_t new_len = strlen(text);
        char *buf = (char *)heap_caps_realloc(chat_history, old_len + new_len + 3,
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!buf) buf = (char *)realloc(chat_history, old_len + new_len + 3);
        if (buf) {
            buf[old_len] = '\n';
            buf[old_len + 1] = '\n';
            memcpy(buf + old_len + 2, text, new_len + 1);
            chat_history = buf;
        }
    } else {
        size_t len = strlen(text);
        chat_history = (char *)heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (chat_history) {
            memcpy(chat_history, text, len + 1);
        } else {
            chat_history = strdup(text);
        }
    }

    if (chat_history) {
        lv_label_set_text(response_label, chat_history);
        if (scroll_cont) {
            lv_obj_scroll_to_y(scroll_cont, LV_COORD_MAX, LV_ANIM_ON);
        }
    }
}

// Show transient status (MUST be called on main LVGL thread).
static void chat_show_status(const char *status)
{
    if (chat_history && chat_history[0]) {
        size_t h_len = strlen(chat_history);
        size_t s_len = strlen(status);
        char *buf = (char *)malloc(h_len + s_len + 3);
        if (buf) {
            memcpy(buf, chat_history, h_len);
            buf[h_len] = '\n';
            buf[h_len + 1] = '\n';
            memcpy(buf + h_len + 2, status, s_len + 1);
            lv_label_set_text(response_label, buf);
            free(buf);
            if (scroll_cont) {
                lv_obj_scroll_to_y(scroll_cont, LV_COORD_MAX, LV_ANIM_ON);
            }
            return;
        }
    }
    lv_label_set_text(response_label, status);
}

static void chat_clear()
{
    if (chat_history) { free(chat_history); chat_history = NULL; }
    if (response_label) lv_label_set_text(response_label, "");
}

// Thread-safe: post a UI message from any task
static void ui_post(int type, const char *text)
{
    if (!ui_queue) return;
    ui_msg_t msg;
    msg.type = type;
    msg.text = strdup(text);
    if (xQueueSend(ui_queue, &msg, pdMS_TO_TICKS(2000)) != pdTRUE) {
        free(msg.text);
    }
}

// LVGL timer callback: drain queue on main thread
static void ui_timer_cb(lv_timer_t *t)
{
    ui_msg_t msg;
    while (xQueueReceive(ui_queue, &msg, 0) == pdTRUE) {
        if (msg.type == UI_MSG_APPEND) chat_append(msg.text);
        else if (msg.type == UI_MSG_STATUS) chat_show_status(msg.text);
        free(msg.text);
    }
}

static void ai_audio_task(void *param)
{
#ifdef GEMINI_API_KEY
    printf("[VoiceAI] ai_audio_task: sending %zu bytes of audio\n", wav_size);
    ui_post(UI_MSG_STATUS, "Processing...");

    gemini_response_t resp = gemini_send_audio(wav_buffer, wav_size, GEMINI_API_KEY);

    // Free WAV buffer
    if (wav_buffer) { free(wav_buffer); wav_buffer = NULL; wav_size = 0; }

    if (resp.success) {
        printf("[VoiceAI] ai_audio_task: success, response len=%zu\n", resp.text.length());
        ui_post(UI_MSG_APPEND, resp.text.c_str());
    } else {
        printf("[VoiceAI] ai_audio_task: error: %s\n", resp.error.c_str());
        char buf[512];
        snprintf(buf, sizeof(buf), "Error: %s", resp.error.c_str());
        ui_post(UI_MSG_APPEND, buf);
    }
#else
    printf("[VoiceAI] ai_audio_task: no API key defined\n");
    ui_post(UI_MSG_APPEND, "No API key. Define GEMINI_API_KEY in config_keys.h");
#endif

    ai_task = NULL;
    vTaskDelete(NULL);
}

static void ai_text_task(void *param)
{
    char *prompt = (char *)param;
#ifdef GEMINI_API_KEY
    printf("[VoiceAI] ai_text_task: prompt=\"%s\"\n", prompt);
    ui_post(UI_MSG_STATUS, "Thinking...");

    gemini_response_t resp = gemini_send_text(prompt, GEMINI_API_KEY);

    if (resp.success) {
        printf("[VoiceAI] ai_text_task: success, response len=%zu\n", resp.text.length());
        ui_post(UI_MSG_APPEND, resp.text.c_str());
    } else {
        printf("[VoiceAI] ai_text_task: error: %s\n", resp.error.c_str());
        char buf[512];
        snprintf(buf, sizeof(buf), "Error: %s", resp.error.c_str());
        ui_post(UI_MSG_APPEND, buf);
    }
#else
    printf("[VoiceAI] ai_text_task: no API key defined\n");
    ui_post(UI_MSG_APPEND, "No API key. Define GEMINI_API_KEY in config_keys.h");
#endif

    free(prompt);
    ai_task = NULL;
    vTaskDelete(NULL);
}

static void record_and_send_task(void *param)
{
    printf("[VoiceAI] record_and_send_task: starting 5s recording\n");
    ui_post(UI_MSG_STATUS, "Recording 5 sec...");

    wav_buffer = NULL;
    wav_size = 0;
    instance.codec.recordWAV(5, &wav_buffer, &wav_size, 16000, 1);

    if (wav_buffer && wav_size > 0) {
        printf("[VoiceAI] record_and_send_task: recorded %zu bytes\n", wav_size);
        ui_post(UI_MSG_APPEND, "> [Audio]");
#ifdef GEMINI_API_KEY
        ui_post(UI_MSG_STATUS, "Processing...");
        gemini_response_t resp = gemini_send_audio(wav_buffer, wav_size, GEMINI_API_KEY);
        free(wav_buffer); wav_buffer = NULL; wav_size = 0;

        if (resp.success) {
            printf("[VoiceAI] record_and_send_task: success, response len=%zu\n", resp.text.length());
            ui_post(UI_MSG_APPEND, resp.text.c_str());
        } else {
            printf("[VoiceAI] record_and_send_task: error: %s\n", resp.error.c_str());
            char buf[512];
            snprintf(buf, sizeof(buf), "Error: %s", resp.error.c_str());
            ui_post(UI_MSG_APPEND, buf);
        }
#else
        free(wav_buffer); wav_buffer = NULL; wav_size = 0;
        printf("[VoiceAI] record_and_send_task: no API key defined\n");
        ui_post(UI_MSG_APPEND, "No API key. Define GEMINI_API_KEY in config_keys.h");
#endif
    } else {
        printf("[VoiceAI] record_and_send_task: recording failed (buf=%p, size=%zu)\n", wav_buffer, wav_size);
        ui_post(UI_MSG_APPEND, "Recording failed");
    }

    ai_task = NULL;
    vTaskDelete(NULL);
}
#endif

static void keyboard_read_cb(int state, char &c)
{
#ifdef ARDUINO
    if (state != 1) return;
    if (!scroll_cont) return;

    int page_h = lv_obj_get_height(scroll_cont);
    if (page_h < 40) page_h = 200;
    int top = lv_obj_get_scroll_top(scroll_cont);
    int bottom = lv_obj_get_scroll_bottom(scroll_cont);
    int dy = 0;

    switch (c) {
    case '2': dy = (top > 30) ? 30 : top; break;           // Space+W scroll up
    case '/': dy = (bottom > 30) ? -30 : -bottom; break;   // Space+S scroll down
    case '0': dy = (top > page_h-20) ? (page_h-20) : top; break;  // Space+P page up
    case ',': dy = (bottom > page_h-20) ? -(page_h-20) : -bottom; break; // Space+N page down
    default: return;
    }

    printf("[VoiceAI] scroll: key='%c' dy=%d top=%d bottom=%d\n", c, dy, top, bottom);
    if (dy != 0) lv_obj_scroll_by(scroll_cont, 0, dy, LV_ANIM_ON);
    c = 0;
#endif
}

static void mic_btn_event_cb(lv_event_t *e)
{
#ifdef ARDUINO
    printf("[VoiceAI] mic button clicked, ai_task=%p\n", ai_task);
    if (ai_task) return;
    xTaskCreatePinnedToCore(record_and_send_task, "ai_rec", 16384, NULL, 5, &ai_task, 0);
#endif
}

static void clear_btn_event_cb(lv_event_t *e)
{
#ifdef ARDUINO
    printf("[VoiceAI] clear button clicked\n");
    chat_clear();
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
            } else {
                lv_group_set_editing(lv_obj_get_group(ta), true);
                enable_keyboard();
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
            printf("[VoiceAI] ta_event: ENTER, text=\"%s\" ai_task=%p\n", text ? text : "(null)", ai_task);
            if (text && text[0] != '\0' && !ai_task) {
#ifdef ARDUINO
                // Append query to chat history
                size_t len = strlen(text);
                char *q = (char *)malloc(len + 3);
                if (q) {
                    q[0] = '>'; q[1] = ' ';
                    memcpy(q + 2, text, len + 1);
                    chat_append(q);
                    free(q);
                }
                char *prompt = strdup(text);
                if (prompt) {
                    xTaskCreatePinnedToCore(ai_text_task, "ai_text", 16384, prompt, 5, &ai_task, 0);
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
                printf("[VoiceAI] ta_event(keypad): ENTER, text=\"%s\" ai_task=%p\n", text ? text : "(null)", ai_task);
                if (text && text[0] != '\0' && !ai_task) {
#ifdef ARDUINO
                    size_t len = strlen(text);
                    char *q = (char *)malloc(len + 3);
                    if (q) {
                        q[0] = '>'; q[1] = ' ';
                        memcpy(q + 2, text, len + 1);
                        chat_append(q);
                        free(q);
                    }
                    char *prompt = strdup(text);
                    if (prompt) {
                        xTaskCreatePinnedToCore(ai_text_task, "ai_text", 16384, prompt, 5, &ai_task, 0);
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
        printf("[VoiceAI] exit\n");
#ifdef ARDUINO
        if (ai_task) { printf("[VoiceAI] killing ai_task\n"); vTaskDelete(ai_task); ai_task = NULL; }
        if (wav_buffer) { free(wav_buffer); wav_buffer = NULL; }
        if (ui_timer) { lv_timer_delete(ui_timer); ui_timer = NULL; }
        if (ui_queue) {
            ui_msg_t msg;
            while (xQueueReceive(ui_queue, &msg, 0) == pdTRUE) free(msg.text);
            vQueueDelete(ui_queue); ui_queue = NULL;
        }
        hw_set_keyboard_read_callback(NULL);
        scroll_cont = NULL;
        chat_clear();
#if LV_USE_TINY_TTF
        if (ttf_font) { lv_tiny_ttf_destroy(ttf_font); ttf_font = NULL; }
        if (ttf_data) { free(ttf_data); ttf_data = NULL; }
#endif
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
    printf("[VoiceAI] enter\n");
#ifdef ARDUINO
    ui_queue = xQueueCreate(8, sizeof(ui_msg_t));
    ui_timer = lv_timer_create(ui_timer_cb, 100, NULL);
#endif
    menu = create_menu(parent, back_event_handler);
    lv_obj_t *main_page = lv_menu_page_create(menu, NULL);

    lv_obj_t *cont = lv_obj_create(main_page);
    lv_obj_set_size(cont, lv_pct(100), lv_pct(100));
    lv_obj_set_style_border_width(cont, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont, 5, LV_PART_MAIN);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);

    // Scrollable response container
    scroll_cont = lv_obj_create(cont);
    lv_obj_set_width(scroll_cont, lv_pct(100));
    lv_obj_set_flex_grow(scroll_cont, 1);
    lv_obj_set_style_border_width(scroll_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scroll_cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(scroll_cont, 0, LV_PART_MAIN);

    response_label = lv_label_create(scroll_cont);
    lv_obj_set_width(response_label, lv_pct(100));
    lv_label_set_long_mode(response_label, LV_LABEL_LONG_WRAP);

#ifdef ARDUINO
    // Set CJK fallback font for response label
    response_font = *MAIN_FONT;
    response_font.fallback = NULL;

#if LV_USE_TINY_TTF
    // Try loading TTF font from SD card for CJK support
    ttf_font = NULL;
    ttf_data = NULL;
    printf("[VoiceAI] TTF: locking SPI...\n");
    instance.lockSPI();
    if (hw_sd_begin()) {
        printf("[VoiceAI] TTF: SD mounted, opening fonts/dict_font.ttf\n");
        File f = SD.open("/fonts/dict_font.ttf", FILE_READ);
        if (f) {
            size_t fsize = f.size();
            printf("[VoiceAI] TTF: file opened, size=%zu bytes\n", fsize);
            size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
            printf("[VoiceAI] TTF: free PSRAM: %zu\n", free_psram);
            ttf_data = (uint8_t *)heap_caps_malloc(fsize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (ttf_data) {
                printf("[VoiceAI] TTF: malloc OK, reading in chunks...\n");
                size_t total_read = 0;
                while (total_read < fsize) {
                    size_t to_read = fsize - total_read;
                    if (to_read > 4096) to_read = 4096;
                    size_t n = f.read(ttf_data + total_read, to_read);
                    if (n == 0) break;
                    total_read += n;
                }
                printf("[VoiceAI] TTF: read %zu / %zu bytes\n", total_read, fsize);
                if (total_read == fsize) {
                    ttf_font = lv_tiny_ttf_create_data(ttf_data, fsize, 16);
                    if (ttf_font) {
                        printf("[VoiceAI] TTF: font created OK, line_height=%d\n",
                               ttf_font->line_height);
                        response_font.fallback = ttf_font;
                    } else {
                        printf("[VoiceAI] TTF: lv_tiny_ttf_create_data FAILED\n");
                        free(ttf_data); ttf_data = NULL;
                    }
                } else {
                    printf("[VoiceAI] TTF: read incomplete, aborting\n");
                    free(ttf_data); ttf_data = NULL;
                }
            } else {
                printf("[VoiceAI] TTF: malloc FAILED for %zu bytes\n", fsize);
            }
            f.close();
        } else {
            printf("[VoiceAI] TTF: file not found on SD\n");
        }
    } else {
        printf("[VoiceAI] TTF: hw_sd_begin FAILED\n");
    }
    instance.unlockSPI();
    printf("[VoiceAI] TTF: done, ttf_font=%p\n", ttf_font);
#else
    printf("[VoiceAI] TTF: DISABLED (LV_USE_TINY_TTF=0 in lv_conf.h)\n");
#endif

    lv_obj_set_style_text_font(response_label, &response_font, LV_PART_MAIN);
#endif

#ifdef GEMINI_API_KEY
    if (!hw_get_wifi_connected()) {
        lv_label_set_text(response_label, "WiFi required. Connect first.");
    } else {
        lv_label_set_text(response_label, "");
    }
#else
    lv_label_set_text(response_label, "Set GEMINI_API_KEY in config_keys.h");
#endif

    // Input row: textarea + mic button + clear button
    lv_obj_t *input_row = lv_obj_create(cont);
    lv_obj_set_size(input_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(input_row, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(input_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(input_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(input_row, 5, LV_PART_MAIN);
    lv_obj_set_flex_flow(input_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(input_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_remove_flag(input_row, LV_OBJ_FLAG_SCROLLABLE);

    input_ta = lv_textarea_create(input_row);
    lv_obj_set_height(input_ta, 40);
    lv_obj_set_flex_grow(input_ta, 1);
    lv_textarea_set_placeholder_text(input_ta, "Ask a question...");
    lv_textarea_set_one_line(input_ta, true);
    lv_textarea_set_max_length(input_ta, 256);
    lv_obj_add_event_cb(input_ta, ta_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *mic_btn = lv_btn_create(input_row);
    lv_obj_set_size(mic_btn, 40, 40);
    lv_obj_add_event_cb(mic_btn, mic_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *mic_label = lv_label_create(mic_btn);
    lv_label_set_text(mic_label, LV_SYMBOL_AUDIO);
    lv_obj_center(mic_label);

    lv_obj_t *clear_btn = lv_btn_create(input_row);
    lv_obj_set_size(clear_btn, 40, 40);
    lv_obj_add_event_cb(clear_btn, clear_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *clear_label = lv_label_create(clear_btn);
    lv_label_set_text(clear_label, LV_SYMBOL_TRASH);
    lv_obj_center(clear_label);

    lv_menu_set_page(menu, main_page);

    // Focus and enable keyboard
    lv_group_add_obj(lv_group_get_default(), input_ta);
    lv_group_add_obj(lv_group_get_default(), mic_btn);
    lv_group_add_obj(lv_group_get_default(), clear_btn);
    lv_group_focus_obj(input_ta);
    lv_group_set_editing(lv_group_get_default(), true);
    lv_obj_add_state(input_ta, (lv_state_t)(LV_STATE_FOCUSED | LV_STATE_EDITED));
    enable_keyboard();
    hw_set_keyboard_read_callback(keyboard_read_cb);

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
