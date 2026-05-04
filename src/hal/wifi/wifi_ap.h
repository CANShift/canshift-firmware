#pragma once
// wifi_ap.h — WiFi AP mode + HTTP OTA server
//
// Started on demand via BLE CMD {"cmd":"start_wifi_ap"}.
// Auto-stops after BLE_WIFI_AP_TIMEOUT_MS (5 minutes) or after a successful OTA.
//
// SSID:     CANShift-XXXX (last 2 bytes of MAC, uppercase hex)
// Password: BLE_WIFI_AP_PASSWORD (defined in app_config.h)
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

} // namespace WifiAp

#endif // APP_BLE_ENABLED
