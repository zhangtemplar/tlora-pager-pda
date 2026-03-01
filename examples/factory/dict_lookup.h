/**
 * @file      dict_lookup.h
 * @author    LilyGo
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-01
 * @brief     Dictionary lookup backend (offline SD card + online API).
 *
 * Supported offline formats on SD card:
 *   - StarDict: Place .ifo, .idx, and .dict (or .dict.dz) files in /stardict/ on SD.
 *               The .ifo file must contain wordcount and idxfilesize fields.
 *               Compressed .dict.dz is NOT supported (use uncompressed .dict).
 *   - Custom binary: /dict_en.idx + /dict_en.dat (binary search format).
 */
#pragma once

#include <stdint.h>
#include <string>

using namespace std;

typedef struct {
    string word;
    string phonetic;
    string part_of_speech;
    string definition;
    bool found;
} dict_result_t;

/**
 * @brief Look up an English word using the online Free Dictionary API.
 * @param word The word to look up.
 * @param result Output result struct.
 * @return true if found.
 */
bool dict_lookup_online(const char *word, dict_result_t &result);

/**
 * @brief Look up a word from StarDict files on SD card.
 * @param word The word to look up (case-insensitive).
 * @param result Output result struct.
 * @return true if found.
 */
bool dict_lookup_stardict(const char *word, dict_result_t &result);

/**
 * @brief Look up an English word from the custom binary dictionary on SD card.
 * @param word The word to look up.
 * @param result Output result struct.
 * @return true if found.
 */
bool dict_lookup_offline_en(const char *word, dict_result_t &result);

/**
 * @brief Check if StarDict dictionary files are available on SD card.
 * @return true if at least one .ifo file found in /stardict/.
 */
bool dict_stardict_available();

/**
 * @brief Check if the custom binary offline dictionary is available on SD card.
 * @return true if dictionary files exist.
 */
bool dict_offline_en_available();
