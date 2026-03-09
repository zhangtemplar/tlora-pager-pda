# PDA — New Features & Setup Guide

This is completely built by CLAUDE with 75k output tokens and estimated cost is $10. This document covers the new apps and enhancements added to the LilyGo PDA firmware.

## Setup

### 1. API Keys

Copy the example config and fill in your credentials:

```bash
cp config_keys.h.example config_keys.h
```

Edit `config_keys.h` and set:

| Key | Required For | Where to Get |
|-----|-------------|--------------|
| `WIFI_SSID` / `WIFI_PASSWORD` | All online features | Your WiFi router |
| `GEMINI_API_KEY` | Voice AI | [Google AI Studio](https://aistudio.google.com/app/apikey) |
| `OWM_API_KEY` | Weather | [OpenWeatherMap](https://openweathermap.org/api) (free tier) |
| `WEATHER_DEFAULT_COORDS` | Weather (no GPS) | `"lat=XX.XX&lon=YY.YY"` — find yours at [latlong.net](https://www.latlong.net) |

`config_keys.h` is gitignored so your keys stay private.

### 2. SD Card (Optional)

Place these files on the SD card for additional features:

```
/fonts/dict_font.ttf       — CJK/Unicode TTF font for Chinese support in Voice AI & Dictionary
/stardict/<name>.ifo       — StarDict dictionary files (supports multiple dictionaries)
/stardict/<name>.idx
/stardict/<name>.dict
/dict_en.idx               — Custom binary English dictionary (alternative format)
/dict_en.dat
```

### 3. LVGL TTF Support

To enable TTF font loading, set `LV_USE_TINY_TTF` to `1` in your Arduino libraries copy of `lv_conf.h`:

```
Arduino/libraries/LilyGoLib/src/lv_conf.h
```

## New Apps

### Voice AI

Chat with Google Gemini using text or voice input.

- Type a question and press **Enter** to send
- Tap the **mic button** to record 5 seconds of audio, which is transcribed and sent to Gemini
- Tap the **clear button** to reset the conversation
- Responses support Chinese characters (with TTF font on SD card)
- Chat history scrolls automatically; use **Space+W/S** to scroll manually

### Dictionary

Offline and online word lookup.

- Supports multiple StarDict dictionaries on SD card — select from the dropdown
- Falls back to online lookup via [dictionaryapi.dev](https://dictionaryapi.dev) when WiFi is connected
- Prefix search shows "Did you mean..." suggestions for misspellings
- Chinese definitions render correctly with TTF font

### Calculator

Scientific calculator with expression evaluation.

- Supports: `+`, `-`, `*`, `/`, `^` (power), `%` (modulo), parentheses
- Functions: `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `sqrt`, `log`, `ln`, `abs`, `pi`, `e`
- Trigonometric functions use degrees by default
- Calculation history shown inline (gray text, scrollable)

### Weather

Current conditions + 24-hour and 8-day forecast.

- Uses device GPS coordinates when available, falls back to `WEATHER_DEFAULT_COORDS`
- Shows temperature, humidity, wind, pressure, UV index
- Forecast tables display correct local time using the API's timezone offset
- Data cached for 1 hour; past entries are filtered out automatically
- Date and day shown next to the location name

### Internet Radio

Stream online radio stations.

### Calendar

Monthly calendar view with navigation.

## Keyboard Shortcuts

### Global (all apps)

| Shortcut | Action |
|----------|--------|
| **Space+D** | Volume up |
| **Space+F** | Volume down |
| **Space+W** | Scroll up |
| **Space+S** | Scroll down |
| **Space+P** | Page up |
| **Space+N** | Page down |

### Boot Button (GPIO 0)

| Action | Result |
|--------|--------|
| **Short press** | Back / Home |
| **Long press** (>1s) | Open Power menu (Sleep / Shutdown) |

### Calculator-Specific

| Key | Inserts |
|-----|---------|
| **$** | `(` |
| **?** | `)` |
| **Space+D** | `+` (intercepted from volume shortcut) |
| **Space+F** | `-` (intercepted from volume shortcut) |

### Weather

| Shortcut | Action |
|----------|--------|
| **Space+W/S** | Scroll forecast up/down |
| **Space+P/N** | Page forecast up/down |

### Calendar

| Shortcut | Action |
|----------|--------|
| **Space+W/S** | Previous/next month |
| **Space+P/N** | Previous/next year |

## Development Stats

All new features in this document were developed with Claude Code (Claude Opus 4) across two sessions.

| Metric | Value |
|--------|-------|
| User interactions | ~27 |
| Tool calls (read/edit/grep/bash) | ~180 |
| Sub-agent calls | ~7 |
| Estimated input tokens | ~330K |
| Estimated output tokens | ~75K |
| Estimated cost | ~$10 |

## Architecture Notes

- **Thread safety**: Voice AI uses a FreeRTOS queue + LVGL timer pattern for UI updates from background tasks. LVGL is not thread-safe — never call `lv_*` functions from FreeRTOS tasks directly.
- **TTF fonts**: Loaded from SD card into PSRAM using Arduino SD API with 4KB chunked reads. Set as a fallback font on the main font for CJK character support.
- **Icon style**: All app icons use black silhouettes on transparent backgrounds (80x80 RGBA). Run `python3 gen_icons.py` to regenerate from the drawing scripts.
