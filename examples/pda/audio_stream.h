/**
 * @file      audio_stream.h
 * @author    LilyGo
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-01
 * @brief     Internet Radio streaming engine - ring buffer + HTTP stream + MP3 decode.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define RADIO_RING_BUF_SIZE  (128 * 1024)  // 128KB ring buffer in PSRAM
#define RADIO_PCM_BUF_SIZE   4608          // PCM output buffer per frame

typedef struct {
    const char *name;
    const char *url;
    const char *genre;
} radio_station_t;

/**
 * @brief Start streaming and playing a radio station.
 * @param url Stream URL (HTTP or HTTPS MP3 stream).
 * @return true if started successfully.
 */
bool radio_stream_start(const char *url);

/**
 * @brief Stop the currently playing radio stream.
 */
void radio_stream_stop();

/**
 * @brief Check if a radio stream is currently playing.
 * @return true if playing.
 */
bool radio_stream_is_playing();

/**
 * @brief Get the current buffer fill percentage.
 * @return 0-100 percentage.
 */
int radio_stream_buffer_pct();

/**
 * @brief Set the playback volume.
 * @param volume 0-100.
 */
void radio_stream_set_volume(uint8_t volume);

/**
 * @brief Get the list of preset stations.
 * @param count Output: number of stations.
 * @return Pointer to station array.
 */
const radio_station_t *radio_get_presets(int *count);
