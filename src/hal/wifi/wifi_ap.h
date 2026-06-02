#pragma once
// wifi_ap.h — WiFi AP mode + HTTP OTA server
//
// Started on demand via either:
//   - BLE CMD {"cmd":"start_wifi_ap"} (mobile bonded peer)
//   - On-device Settings page toggle (#1077 — dash-hosted Studio path)
//   - Boot auto-start when the persisted "auto-start" NVS preference is on
//
// Auto-stops after BLE_WIFI_AP_TIMEOUT_MS (5 minutes) UNLESS the persistent
// auto-start preference is on, in which case the AP stays up until the user
// explicitly toggles it off. The auto-start flag exists so a phone-less user
// can bring up the dash-hosted Studio over WiFi from their laptop's browser
// without a BLE mobile app.
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

/**
 * Persistent "auto-start WiFi AP on boot" preference (NVS namespace "wifi_ap",
 * key "auto"). When true, BootSequence::run() brings the AP up after USB init,
 * and the 5-minute timeout in the AP task is suppressed (the user opted in).
 *
 * Surfaced to the on-device Settings page (#1077) so a phone-less user can
 * enable the dash-hosted Studio path without going through the BLE mobile app.
 */
bool isAutoStartEnabled();

/**
 * Persist the auto-start preference AND act on it immediately:
 *   - true  → writes NVS and calls start() if the AP isn't already up.
 *   - false → writes NVS and calls stop() if the AP is currently up.
 *
 * Safe to call from the UI task — the underlying start()/stop() spawn /
 * signal a separate WiFi task on core 1.
 */
void setAutoStartEnabled(bool enabled);

/**
 * Periodic auto-start retry — call from the UI task. Issue #1263.
 *
 * Boot fires before the LVGL pool, icon cache and per-page widgets have
 * settled, so the largest free internal block can be too small for the
 * WebServer's allocations. Rather than refusing the AP at boot, defer the
 * attempt: this tick checks once per `AUTO_START_RETRY_INTERVAL_MS` whether
 * (a) the user enabled auto-start, (b) the AP isn't already up, and (c) the
 * heap has settled above the safe threshold — and only then calls `start()`.
 *
 * Safe to call every UI tick; cheap when nothing needs doing.
 */
void tickAutoStart();

} // namespace WifiAp

#endif // APP_BLE_ENABLED
