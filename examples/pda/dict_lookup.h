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
 *               Multiple dictionaries are supported - place multiple sets of files.
 *   - Custom binary: /dict_en.idx + /dict_en.dat (binary search format).
 */
#pragma once

#include <stdint.h>
#include <string>

#ifdef ARDUINO
#include <Arduino.h>
#endif

using namespace std;

#define MAX_STARDICT_DICTS  8

typedef struct {
    string word;
    string phonetic;
    string part_of_speech;
    string definition;
    bool found;
} dict_result_t;

typedef struct {
#ifdef ARDUINO
    String name;        // from .ifo "bookname"
    String ifo_path;
    String idx_path;
    String dict_path;
#else
    string name;
    string ifo_path;
    string idx_path;
    string dict_path;
#endif
} dict_info_t;

/**
 * @brief Look up an English word using the online Free Dictionary API.
 * @param word The word to look up.
 * @param result Output result struct.
 * @return true if found.
 */
bool dict_lookup_online(const char *word, dict_result_t &result);

/**
 * @brief Look up a word from StarDict files on SD card (first match across all dicts).
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

/**
 * @brief Scan /stardict/ for all .ifo files and populate the internal dictionary list.
 * @return Number of dictionaries found (0 to MAX_STARDICT_DICTS).
 */
int dict_scan_stardict();

/**
 * @brief Look up a word in a specific StarDict dictionary (by index from scan results).
 * @param dict_index Index of the dictionary (0-based, from scan results).
 * @param word The word to look up (case-insensitive).
 * @param result Output result struct.
 * @return true if found.
 */
bool dict_lookup_stardict_single(int dict_index, const char *word, dict_result_t &result);

/**
 * @brief Look up a word across all scanned StarDict dictionaries (returns first match).
 * @param word The word to look up (case-insensitive).
 * @param result Output result struct.
 * @return true if found.
 */
bool dict_lookup_stardict_all(const char *word, dict_result_t &result);

/**
 * @brief Get count of available StarDict dictionaries (after scan).
 * @return Number of dictionaries found.
 */
int dict_get_stardict_count();

/**
 * @brief Get the display name of a scanned StarDict dictionary.
 * @param index Index of the dictionary (0-based).
 * @return Name string, or NULL if index out of range.
 */
const char *dict_get_stardict_name(int index);
