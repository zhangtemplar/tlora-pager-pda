/**
 * @file      config_keys.h
 * @author    LilyGo
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-01
 * @brief     User-configurable compile-time settings for network-connected features.
 *
 * To enable a feature, uncomment the corresponding #define and fill in your key/value.
 * See config_keys.h.example for a template.
 */
#pragma once

// ---- WiFi Credentials ----
// Primary WiFi network
#define WIFI_SSID         "kant-netgear"
#define WIFI_PASSWORD     "sanfeng12@126.com"

// Optional: Secondary WiFi network (auto-fills password when selected in UI)
// #define WIFI_SSID2        "your-second-ssid"
// #define WIFI_PASSWORD2    "your-second-password"

// ---- Google Gemini API Key (for Voice AI feature) ----
// Get your key at: https://aistudio.google.com/app/apikey
#define GEMINI_API_KEY    "AIzaSyBrliBucTcEw64ZYzML_83m7BtsyFA3C-s"

// ---- OpenWeatherMap API Key (for Weather feature) ----
// Get your free key at: https://openweathermap.org/api
#define OWM_API_KEY       "9290d712d2ab70d07975c207ceab7e8e"

// ---- Weather Notes ----
// Location priority: GPS fix > cached GPS > fallback (San Carlos, CA: 37.49, -122.27)
// GPS coordinates are cached in NVS automatically after first fix.
