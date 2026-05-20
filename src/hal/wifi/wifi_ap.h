#pragma once
// wifi_ap.h — WiFi AP mode + HTTP OTA server
//
// Started on demand via BLE CMD {"cmd":"start_wifi_ap"}.
// Auto-stops after BLE_WIFI_AP_TIMEOUT_MS (5 minutes) or after a successful OTA.
//
// SSID:     CANShift-XXXX (last 2 bytes of MAC, uppercase hex)
// Password: per-device, 32 hex chars (128 bits entropy from esp_fill_random()),
//           generated on first boot and persisted in NVS namespace "wifi_ap",
//           key "pwd". Surfaced to clients via BLE STATUS field "ap_password".
//           Raised from 64 bits (16 chars) in #910 — WPA2 handshake capture
//           within the 5-minute AP window can brute-force 64 bits offline.
// IP:       192.168.4.1 (ESP32 softAP default)
//
// HTTP endpoints:
//   GET  /status  → {"status":"ok","ver":"x.y.z"}
//   POST /ota     → multipart/form-data, field "firmware" = binary
//                   Response: {"status":"ok"} then device reboots
//                             {"status":"error","message":"..."} on failure

#include "app_config.h"

#if APP_BLE_ENABLED

namespace WifiAp {

/** Start the WiFi AP and HTTP server in a background task. No-op if already active. */
void start();

/** Stop the WiFi AP and HTTP server. */
void stop();

/** Returns true if the AP is currently active. */
bool isActive();

/** Returns the AP SSID (valid after start(), empty string before). */
const char *getSsid();

/** Returns the AP password (32 hex chars). Lazy-generates and persists on first call. */
const char *getPassword();

} // namespace WifiAp

#endif // APP_BLE_ENABLED
