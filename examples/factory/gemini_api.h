/**
 * @file      gemini_api.h
 * @author    LilyGo
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-01
 * @brief     Google Gemini API client for Voice AI feature.
 */
#pragma once

#include <stdint.h>
#include <string>

using namespace std;

typedef struct {
    string text;
    bool success;
    string error;
} gemini_response_t;

/**
 * @brief Send audio (WAV) to Gemini for transcription and response.
 * @param wav_data Pointer to WAV data in PSRAM.
 * @param wav_len Length of WAV data.
 * @param api_key Gemini API key string.
 * @return gemini_response_t with text response or error.
 */
gemini_response_t gemini_send_audio(const uint8_t *wav_data, size_t wav_len, const char *api_key);

/**
 * @brief Send a text prompt to Gemini.
 * @param prompt Text prompt string.
 * @param api_key Gemini API key string.
 * @return gemini_response_t with text response or error.
 */
gemini_response_t gemini_send_text(const char *prompt, const char *api_key);
