/**
 * @file      gemini_api.cpp
 * @author    LilyGo
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-01
 * @brief     Google Gemini API client implementation.
 */
#include "gemini_api.h"
#include "http_utils.h"
#include <string.h>

#ifdef ARDUINO
#include <cJSON.h>
#include <esp_heap_caps.h>

static gemini_response_t parse_gemini_response(const char *json)
{
    gemini_response_t resp = {"", false, ""};

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        resp.error = "Failed to parse response JSON";
        return resp;
    }

    // Check for error
    cJSON *error_obj = cJSON_GetObjectItem(root, "error");
    if (error_obj) {
        cJSON *msg = cJSON_GetObjectItem(error_obj, "message");
        if (msg && msg->valuestring) {
            resp.error = msg->valuestring;
        } else {
            resp.error = "API error";
        }
        cJSON_Delete(root);
        return resp;
    }

    // Extract text from: candidates[0].content.parts[0].text
    cJSON *candidates = cJSON_GetObjectItem(root, "candidates");
    if (candidates && cJSON_GetArraySize(candidates) > 0) {
        cJSON *c0 = cJSON_GetArrayItem(candidates, 0);
        cJSON *content = cJSON_GetObjectItem(c0, "content");
        if (content) {
            cJSON *parts = cJSON_GetObjectItem(content, "parts");
            if (parts && cJSON_GetArraySize(parts) > 0) {
                cJSON *p0 = cJSON_GetArrayItem(parts, 0);
                cJSON *text = cJSON_GetObjectItem(p0, "text");
                if (text && text->valuestring) {
                    resp.text = text->valuestring;
                    resp.success = true;
                }
            }
        }
    }

    if (!resp.success && resp.error.empty()) {
        resp.error = "No text in response";
    }

    cJSON_Delete(root);
    return resp;
}

gemini_response_t gemini_send_audio(const uint8_t *wav_data, size_t wav_len, const char *api_key)
{
    gemini_response_t resp = {"", false, ""};

    if (!api_key || api_key[0] == '\0') {
        resp.error = "No API key";
        return resp;
    }

    // Base64 encode WAV
    size_t b64_len = 0;
    char *b64_data = base64_encode_psram(wav_data, wav_len, &b64_len);
    if (!b64_data) {
        resp.error = "Base64 encoding failed (out of memory)";
        return resp;
    }

    // Build JSON request
    // We build it manually to avoid allocating another huge buffer for cJSON
    // Format: {"contents":[{"parts":[{"inline_data":{"mime_type":"audio/wav","data":"..."}},{"text":"..."}]}]}

    const char *system_prompt = "Listen to the audio and respond helpfully. If the audio contains speech, "
                                "respond to what was said. Keep responses concise.";

    // Calculate total size needed
    size_t json_prefix_len = 256 + strlen(system_prompt);
    size_t json_suffix_len = 32;
    size_t total_len = json_prefix_len + b64_len + json_suffix_len;

    char *json_body = (char *)heap_caps_malloc(total_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!json_body) {
        free(b64_data);
        resp.error = "Out of PSRAM for request body";
        return resp;
    }

    int written = snprintf(json_body, json_prefix_len,
                           "{\"contents\":[{\"parts\":[{\"inline_data\":{\"mime_type\":\"audio/wav\",\"data\":\"");
    memcpy(json_body + written, b64_data, b64_len);
    written += b64_len;
    int suffix = snprintf(json_body + written, json_suffix_len,
                          "\"}},{\"text\":\"%s\"}]}]}", system_prompt);
    written += suffix;
    json_body[written] = '\0';

    free(b64_data);

    // Build URL
    char url[256];
    snprintf(url, sizeof(url),
             "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent?key=%s",
             api_key);

    // Send request
    http_response_t http_resp = http_post_large(url, (const uint8_t *)json_body, written,
                                                "application/json", 30000);
    free(json_body);

    if (!http_resp.success) {
        resp.error = "HTTP request failed: " + http_resp.body;
        return resp;
    }

    return parse_gemini_response(http_resp.body.c_str());
}

gemini_response_t gemini_send_text(const char *prompt, const char *api_key)
{
    gemini_response_t resp = {"", false, ""};

    if (!api_key || api_key[0] == '\0') {
        resp.error = "No API key";
        return resp;
    }

    // Build JSON
    cJSON *root = cJSON_CreateObject();
    cJSON *contents = cJSON_CreateArray();
    cJSON *content = cJSON_CreateObject();
    cJSON *parts = cJSON_CreateArray();
    cJSON *part = cJSON_CreateObject();

    cJSON_AddStringToObject(part, "text", prompt);
    cJSON_AddItemToArray(parts, part);
    cJSON_AddItemToObject(content, "parts", parts);
    cJSON_AddItemToArray(contents, content);
    cJSON_AddItemToObject(root, "contents", contents);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_str) {
        resp.error = "Failed to build JSON";
        return resp;
    }

    char url[256];
    snprintf(url, sizeof(url),
             "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent?key=%s",
             api_key);

    string body_str = json_str;
    free(json_str);

    http_response_t http_resp = http_post(url, body_str, "application/json", "", 15000);

    if (!http_resp.success) {
        resp.error = "HTTP request failed: " + http_resp.body;
        return resp;
    }

    return parse_gemini_response(http_resp.body.c_str());
}

#else
// Desktop stubs
gemini_response_t gemini_send_audio(const uint8_t *wav_data, size_t wav_len, const char *api_key)
{
    return {"Desktop stub", false, "Not supported"};
}
gemini_response_t gemini_send_text(const char *prompt, const char *api_key)
{
    return {"Desktop stub", false, "Not supported"};
}
#endif
