/**
 * @file      dict_lookup.cpp
 * @author    LilyGo
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-01
 * @brief     Dictionary lookup backend implementation.
 *
 * Supports:
 *   - StarDict format (.ifo/.idx/.dict on SD card at /stardict/)
 *     Multiple dictionaries are supported - scans all .ifo files.
 *   - Custom binary format (dict_en.idx + dict_en.dat on SD root)
 *   - Online lookup via Free Dictionary API
 */
#include "dict_lookup.h"
#include "http_utils.h"
#include "hal_interface.h"
#include <string.h>

#ifdef ARDUINO
#include <SD.h>
#include <cJSON.h>

// ---- Multi-StarDict support ----

static dict_info_t stardict_dicts[MAX_STARDICT_DICTS];
static int stardict_count = 0;
static bool stardict_scanned = false;

static void parse_ifo_bookname(const char *ifo_path, String &bookname)
{
    File f = SD.open(ifo_path, FILE_READ);
    if (!f) {
        bookname = "Unknown";
        return;
    }
    bookname = "Unknown";
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.startsWith("bookname=")) {
            bookname = line.substring(9);
            break;
        }
    }
    f.close();
}

int dict_scan_stardict()
{
    if (stardict_scanned) return stardict_count;
    stardict_scanned = true;
    stardict_count = 0;

    File dir = SD.open("/stardict");
    if (!dir || !dir.isDirectory()) return 0;

    File entry;
    while ((entry = dir.openNextFile()) && stardict_count < MAX_STARDICT_DICTS) {
        String name = entry.name();
        entry.close();

        if (!name.endsWith(".ifo")) continue;

        // Extract base name (without .ifo extension)
        String base_name = name.substring(0, name.length() - 4);
        String ifo_path = String("/stardict/") + name;
        String idx_path = String("/stardict/") + base_name + ".idx";
        String dict_path = String("/stardict/") + base_name + ".dict";

        // Verify companion files exist
        if (!SD.exists(idx_path.c_str()) || !SD.exists(dict_path.c_str())) {
            continue;
        }

        // Parse bookname from .ifo
        String bookname;
        parse_ifo_bookname(ifo_path.c_str(), bookname);

        dict_info_t &d = stardict_dicts[stardict_count];
        d.name = bookname;
        d.ifo_path = ifo_path;
        d.idx_path = idx_path;
        d.dict_path = dict_path;
        stardict_count++;
    }
    dir.close();

    return stardict_count;
}

bool dict_stardict_available()
{
    dict_scan_stardict();
    return stardict_count > 0;
}

int dict_get_stardict_count()
{
    dict_scan_stardict();
    return stardict_count;
}

const char *dict_get_stardict_name(int index)
{
    if (index < 0 || index >= stardict_count) return NULL;
    return stardict_dicts[index].name.c_str();
}

// Look up in a single StarDict dictionary by its paths
static bool stardict_lookup_in(const String &idx_path, const String &dict_path,
                               const char *word, dict_result_t &result)
{
    result.found = false;

    File idx_file = SD.open(idx_path.c_str(), FILE_READ);
    if (!idx_file) return false;

    // Linear scan through the .idx file
    // Each entry: word\0 + 4-byte offset (big-endian) + 4-byte size (big-endian)
    char entry_word[256];
    size_t file_size = idx_file.size();

    while (idx_file.position() < file_size) {
        // Read word (null-terminated string)
        int i = 0;
        while (i < 255) {
            int c = idx_file.read();
            if (c < 0) break;
            if (c == '\0') break;
            entry_word[i++] = (char)c;
        }
        entry_word[i] = '\0';

        if (idx_file.position() >= file_size) break;

        // Read 4-byte offset and 4-byte size (big-endian)
        uint8_t buf[8];
        if (idx_file.read(buf, 8) != 8) break;

        uint32_t data_offset = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
                               ((uint32_t)buf[2] << 8) | buf[3];
        uint32_t data_size = ((uint32_t)buf[4] << 24) | ((uint32_t)buf[5] << 16) |
                             ((uint32_t)buf[6] << 8) | buf[7];

        if (strcasecmp(word, entry_word) == 0) {
            idx_file.close();

            // Read definition from .dict file
            File dict_file = SD.open(dict_path.c_str(), FILE_READ);
            if (!dict_file) return false;

            // Limit read size to prevent OOM
            if (data_size > 4096) data_size = 4096;

            dict_file.seek(data_offset);
            char *def_buf = (char *)malloc(data_size + 1);
            if (!def_buf) { dict_file.close(); return false; }

            dict_file.read((uint8_t *)def_buf, data_size);
            def_buf[data_size] = '\0';
            dict_file.close();

            result.word = entry_word;
            result.definition = def_buf;
            result.found = true;
            free(def_buf);
            return true;
        }
    }

    idx_file.close();
    return false;
}

bool dict_lookup_stardict(const char *word, dict_result_t &result)
{
    return dict_lookup_stardict_all(word, result);
}

bool dict_lookup_stardict_single(int dict_index, const char *word, dict_result_t &result)
{
    result.found = false;
    dict_scan_stardict();
    if (dict_index < 0 || dict_index >= stardict_count) return false;

    dict_info_t &d = stardict_dicts[dict_index];
    bool found = stardict_lookup_in(d.idx_path, d.dict_path, word, result);
    if (found) {
        // Prepend dictionary name to definition
        string prefix = "[" + string(d.name.c_str()) + "]\n";
        result.definition = prefix + result.definition;
    }
    return found;
}

bool dict_lookup_stardict_all(const char *word, dict_result_t &result)
{
    result.found = false;
    dict_scan_stardict();

    for (int i = 0; i < stardict_count; i++) {
        dict_info_t &d = stardict_dicts[i];
        if (stardict_lookup_in(d.idx_path, d.dict_path, word, result)) {
            // Prepend dictionary name to definition
            string prefix = "[" + string(d.name.c_str()) + "]\n";
            result.definition = prefix + result.definition;
            return true;
        }
    }
    return false;
}

// ---- Online lookup ----

bool dict_lookup_online(const char *word, dict_result_t &result)
{
    result.found = false;
    if (!hw_get_wifi_connected()) return false;

    char url[256];
    snprintf(url, sizeof(url), "https://api.dictionaryapi.dev/api/v2/entries/en/%s", word);

    http_response_t resp = http_get(url, 8000);
    if (!resp.success) return false;

    cJSON *root = cJSON_Parse(resp.body.c_str());
    if (!root) return false;

    // Response is an array
    if (!cJSON_IsArray(root) || cJSON_GetArraySize(root) == 0) {
        cJSON_Delete(root);
        return false;
    }

    cJSON *entry = cJSON_GetArrayItem(root, 0);

    cJSON *word_j = cJSON_GetObjectItem(entry, "word");
    if (word_j && word_j->valuestring) result.word = word_j->valuestring;

    // Phonetic
    cJSON *phonetic = cJSON_GetObjectItem(entry, "phonetic");
    if (phonetic && phonetic->valuestring) result.phonetic = phonetic->valuestring;

    // Get first meaning
    cJSON *meanings = cJSON_GetObjectItem(entry, "meanings");
    if (meanings && cJSON_GetArraySize(meanings) > 0) {
        cJSON *meaning0 = cJSON_GetArrayItem(meanings, 0);

        cJSON *pos = cJSON_GetObjectItem(meaning0, "partOfSpeech");
        if (pos && pos->valuestring) result.part_of_speech = pos->valuestring;

        cJSON *defs = cJSON_GetObjectItem(meaning0, "definitions");
        if (defs && cJSON_GetArraySize(defs) > 0) {
            string all_defs;
            int n = cJSON_GetArraySize(defs);
            if (n > 3) n = 3;
            for (int i = 0; i < n; i++) {
                cJSON *def_item = cJSON_GetArrayItem(defs, i);
                cJSON *def_text = cJSON_GetObjectItem(def_item, "definition");
                if (def_text && def_text->valuestring) {
                    char num[8];
                    snprintf(num, sizeof(num), "%d. ", i + 1);
                    all_defs += num;
                    all_defs += def_text->valuestring;
                    all_defs += "\n";
                }
            }
            result.definition = all_defs;
        }
    }

    result.found = true;
    cJSON_Delete(root);
    return true;
}

// ---- Custom binary format ----

bool dict_offline_en_available()
{
    return SD.exists("/dict_en.idx") && SD.exists("/dict_en.dat");
}

bool dict_lookup_offline_en(const char *word, dict_result_t &result)
{
    result.found = false;
    if (!dict_offline_en_available()) return false;

    File idx_file = SD.open("/dict_en.idx", FILE_READ);
    if (!idx_file) return false;

    // Binary search in index file
    // Index format: word\0 + 4-byte offset (big-endian) + 2-byte length (big-endian)
    size_t file_size = idx_file.size();
    size_t low = 0, high = file_size;
    char buf[256];
    bool found = false;
    uint32_t data_offset = 0;
    uint16_t data_len = 0;

    while (low < high) {
        size_t mid = (low + high) / 2;

        // Seek backwards to find start of entry (after a \0)
        idx_file.seek(mid);
        while (mid > low) {
            mid--;
            idx_file.seek(mid);
            char c = idx_file.read();
            if (c == '\0') {
                mid++;
                break;
            }
        }
        if (mid == low && low > 0) {
            idx_file.seek(mid);
        }

        // Read the word at this position
        idx_file.seek(mid);
        int i = 0;
        while (i < 255) {
            int c = idx_file.read();
            if (c < 0 || c == '\0') break;
            buf[i++] = (char)c;
        }
        buf[i] = '\0';

        int cmp = strcasecmp(word, buf);
        if (cmp == 0) {
            // Read offset and length
            uint8_t offset_bytes[4];
            uint8_t len_bytes[2];
            idx_file.read(offset_bytes, 4);
            idx_file.read(len_bytes, 2);
            data_offset = ((uint32_t)offset_bytes[0] << 24) | ((uint32_t)offset_bytes[1] << 16) |
                          ((uint32_t)offset_bytes[2] << 8) | offset_bytes[3];
            data_len = ((uint16_t)len_bytes[0] << 8) | len_bytes[1];
            found = true;
            break;
        } else if (cmp < 0) {
            high = mid;
        } else {
            // Skip past this entry's offset+length to advance
            idx_file.seek(idx_file.position() + 6);
            low = idx_file.position();
        }
    }

    idx_file.close();

    if (!found) return false;

    // Read definition from data file
    File dat_file = SD.open("/dict_en.dat", FILE_READ);
    if (!dat_file) return false;

    dat_file.seek(data_offset);
    char *def_buf = (char *)malloc(data_len + 1);
    if (!def_buf) { dat_file.close(); return false; }

    dat_file.read((uint8_t *)def_buf, data_len);
    def_buf[data_len] = '\0';
    dat_file.close();

    result.word = word;
    result.definition = def_buf;
    result.found = true;
    free(def_buf);
    return true;
}

#else
// Desktop stubs
bool dict_lookup_online(const char *word, dict_result_t &result) { result.found = false; return false; }
bool dict_lookup_stardict(const char *word, dict_result_t &result) { result.found = false; return false; }
bool dict_lookup_offline_en(const char *word, dict_result_t &result) { result.found = false; return false; }
bool dict_stardict_available() { return false; }
bool dict_offline_en_available() { return false; }
int dict_scan_stardict() { return 0; }
bool dict_lookup_stardict_single(int dict_index, const char *word, dict_result_t &result) { result.found = false; return false; }
bool dict_lookup_stardict_all(const char *word, dict_result_t &result) { result.found = false; return false; }
int dict_get_stardict_count() { return 0; }
const char *dict_get_stardict_name(int index) { return NULL; }
#endif
