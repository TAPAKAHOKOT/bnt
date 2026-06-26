#pragma once

// Copy this file to secrets.h and fill in your local values.
//   cp include/secrets.example.h include/secrets.h
// secrets.h is gitignored — never commit real credentials.
//
// IMPORTANT: the OpenAI API key must NEVER live in firmware.
// Only Wi-Fi credentials and the local backend URL belong here.

#define WIFI_SSID "your-wifi-ssid"
#define WIFI_PASSWORD "your-wifi-password"

// Local backend reachable from the ESP32 on the same Wi-Fi network.
// Example: laptop running the backend at 192.168.1.50:8000
#define BNT_BACKEND_URL "http://192.168.1.50:8000/ask-audio"

// Shared secret sent as `Authorization: Bearer <token>`. Must match the
// backend's BNT_API_TOKEN env var, otherwise the backend returns 401.
#define BNT_API_TOKEN "your-shared-api-token"
