/**
 * @file      audio_stream.cpp
 * @author    LilyGo
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-01
 * @brief     Internet Radio streaming engine implementation.
 */
#include "audio_stream.h"
#include "hal_interface.h"

#ifdef ARDUINO
#include <WiFi.h>
#include <WiFiClient.h>
#include <LilyGoLib.h>
#include <mp3dec.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// Preset stations
static const radio_station_t presets[] = {
    {"SomaFM Groove Salad", "http://ice1.somafm.com/groovesalad-128-mp3", "Ambient"},
    {"Jazz24",              "http://live.wostreaming.net/direct/ppm-jazz24mp3-ibc1", "Jazz"},
    {"Classic Rock Florida","http://198.58.98.83:8258/stream", "Rock"},
    {"Lounge (SomaFM)",    "http://ice1.somafm.com/lush-128-mp3", "Lounge"},
    {"Drone Zone (SomaFM)","http://ice1.somafm.com/dronezone-128-mp3", "Ambient"},
};
#define PRESET_COUNT (sizeof(presets) / sizeof(presets[0]))

// Ring buffer
static uint8_t *ring_buf = NULL;
static volatile size_t rb_write_pos = 0;
static volatile size_t rb_read_pos = 0;
static volatile size_t rb_fill = 0;
static SemaphoreHandle_t rb_mutex = NULL;

// Task handles
static TaskHandle_t stream_task = NULL;
static TaskHandle_t decode_task = NULL;
static volatile bool stream_running = false;
static volatile bool decode_running = false;
static volatile bool stop_requested = false;

static size_t rb_available() { return rb_fill; }
static size_t rb_free() { return RADIO_RING_BUF_SIZE - rb_fill; }

static size_t rb_write(const uint8_t *data, size_t len)
{
    if (len == 0) return 0;
    xSemaphoreTake(rb_mutex, portMAX_DELAY);
    size_t can_write = rb_free();
    if (len > can_write) len = can_write;
    for (size_t i = 0; i < len; i++) {
        ring_buf[rb_write_pos] = data[i];
        rb_write_pos = (rb_write_pos + 1) % RADIO_RING_BUF_SIZE;
    }
    rb_fill += len;
    xSemaphoreGive(rb_mutex);
    return len;
}

static size_t rb_read(uint8_t *data, size_t len)
{
    if (len == 0) return 0;
    xSemaphoreTake(rb_mutex, portMAX_DELAY);
    size_t can_read = rb_available();
    if (len > can_read) len = can_read;
    for (size_t i = 0; i < len; i++) {
        data[i] = ring_buf[rb_read_pos];
        rb_read_pos = (rb_read_pos + 1) % RADIO_RING_BUF_SIZE;
    }
    rb_fill -= len;
    xSemaphoreGive(rb_mutex);
    return len;
}

static void http_stream_task(void *param)
{
    const char *url = (const char *)param;
    stream_running = true;

    WiFiClient client;
    // Parse URL (simplified - handle http:// only for streams)
    String url_str = url;
    String host;
    String path = "/";
    int port = 80;

    if (url_str.startsWith("http://")) {
        url_str = url_str.substring(7);
    }

    int slash_pos = url_str.indexOf('/');
    if (slash_pos > 0) {
        host = url_str.substring(0, slash_pos);
        path = url_str.substring(slash_pos);
    } else {
        host = url_str;
    }

    int colon_pos = host.indexOf(':');
    if (colon_pos > 0) {
        port = host.substring(colon_pos + 1).toInt();
        host = host.substring(0, colon_pos);
    }

    if (!client.connect(host.c_str(), port)) {
        stream_running = false;
        vTaskDelete(NULL);
        return;
    }

    // Send HTTP GET
    client.printf("GET %s HTTP/1.0\r\nHost: %s\r\nIcy-MetaData: 0\r\n\r\n", path.c_str(), host.c_str());

    // Skip HTTP headers
    while (client.connected() && !stop_requested) {
        String line = client.readStringUntil('\n');
        if (line == "\r" || line.length() == 0) break;
    }

    // Stream data into ring buffer
    uint8_t chunk[1024];
    while (client.connected() && !stop_requested) {
        int avail = client.available();
        if (avail > 0) {
            int to_read = avail > (int)sizeof(chunk) ? (int)sizeof(chunk) : avail;
            int got = client.read(chunk, to_read);
            if (got > 0) {
                // Wait for space in ring buffer
                int written = 0;
                while (written < got && !stop_requested) {
                    int w = rb_write(chunk + written, got - written);
                    written += w;
                    if (written < got) {
                        vTaskDelay(pdMS_TO_TICKS(10));
                    }
                }
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    client.stop();
    stream_running = false;
    vTaskDelete(NULL);
}

static void mp3_decode_task(void *param)
{
    decode_running = true;

    HMP3Decoder decoder = MP3InitDecoder();
    if (!decoder) {
        decode_running = false;
        vTaskDelete(NULL);
        return;
    }

    int16_t pcm_buf[RADIO_PCM_BUF_SIZE / 2];
    uint8_t mp3_buf[2048];
    int mp3_buf_fill = 0;
    bool codec_opened = false;

    // Wait for buffer to fill initially (prebuffer 25%)
    while (rb_available() < RADIO_RING_BUF_SIZE / 4 && !stop_requested) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    while (!stop_requested) {
        // Fill mp3_buf from ring buffer
        int need = sizeof(mp3_buf) - mp3_buf_fill;
        int got = rb_read((uint8_t *)mp3_buf + mp3_buf_fill, need);
        mp3_buf_fill += got;

        if (mp3_buf_fill < 128) {
            if (!stream_running) break;
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        // Find sync word
        uint8_t *read_ptr = mp3_buf;
        int bytes_left = mp3_buf_fill;
        int offset = MP3FindSyncWord(read_ptr, bytes_left);
        if (offset < 0) {
            mp3_buf_fill = 0;
            continue;
        }
        read_ptr += offset;
        bytes_left -= offset;

        // Decode one frame
        int err = MP3Decode(decoder, &read_ptr, &bytes_left, pcm_buf, 0);
        if (err == 0) {
            MP3FrameInfo info;
            MP3GetLastFrameInfo(decoder, &info);

            if (!codec_opened) {
                instance.codec.open(info.bitsPerSample, info.nChans, info.samprate);
                codec_opened = true;
            }

            instance.codec.write((uint8_t *)pcm_buf, (info.bitsPerSample / 8) * info.outputSamps);
        }

        // Move remaining data to front
        int consumed = mp3_buf_fill - bytes_left;
        if (bytes_left > 0 && consumed > 0) {
            memmove(mp3_buf, mp3_buf + consumed, bytes_left);
        }
        mp3_buf_fill = bytes_left;
    }

    if (codec_opened) {
        instance.codec.close();
    }
    MP3FreeDecoder(decoder);
    decode_running = false;
    vTaskDelete(NULL);
}

bool radio_stream_start(const char *url)
{
    if (stream_running || decode_running) {
        radio_stream_stop();
    }

    // Allocate ring buffer in PSRAM
    if (!ring_buf) {
        ring_buf = (uint8_t *)heap_caps_malloc(RADIO_RING_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!ring_buf) return false;
    }

    if (!rb_mutex) {
        rb_mutex = xSemaphoreCreateMutex();
    }

    // Reset ring buffer
    rb_write_pos = 0;
    rb_read_pos = 0;
    rb_fill = 0;
    stop_requested = false;

    // Store URL (must persist)
    static char url_buf[256];
    strncpy(url_buf, url, sizeof(url_buf) - 1);
    url_buf[sizeof(url_buf) - 1] = '\0';

    // Start tasks
    xTaskCreatePinnedToCore(http_stream_task, "radio_http", 8192, url_buf, 5, &stream_task, 0);
    xTaskCreatePinnedToCore(mp3_decode_task, "radio_mp3", 16384, NULL, 12, &decode_task, 1);

    return true;
}

void radio_stream_stop()
{
    stop_requested = true;

    // Wait for tasks to finish
    int timeout = 50;
    while ((stream_running || decode_running) && timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
        timeout--;
    }

    // Force delete if still running
    if (stream_task && stream_running) { vTaskDelete(stream_task); stream_running = false; }
    if (decode_task && decode_running) { vTaskDelete(decode_task); decode_running = false; }
    stream_task = NULL;
    decode_task = NULL;

    if (ring_buf) {
        heap_caps_free(ring_buf);
        ring_buf = NULL;
    }
    if (rb_mutex) {
        vSemaphoreDelete(rb_mutex);
        rb_mutex = NULL;
    }

    instance.codec.close();
}

bool radio_stream_is_playing()
{
    return stream_running || decode_running;
}

int radio_stream_buffer_pct()
{
    if (!ring_buf) return 0;
    return (int)((rb_fill * 100) / RADIO_RING_BUF_SIZE);
}

void radio_stream_set_volume(uint8_t volume)
{
    hw_set_volume(volume);
}

const radio_station_t *radio_get_presets(int *count)
{
    *count = PRESET_COUNT;
    return presets;
}

#else
// Desktop stubs
bool radio_stream_start(const char *url) { return false; }
void radio_stream_stop() {}
bool radio_stream_is_playing() { return false; }
int radio_stream_buffer_pct() { return 0; }
void radio_stream_set_volume(uint8_t volume) {}
static const radio_station_t presets[] = {{"Test", "http://test", "Test"}};
const radio_station_t *radio_get_presets(int *count) { *count = 1; return presets; }
#endif
