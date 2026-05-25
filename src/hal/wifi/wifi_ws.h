#pragma once
// wifi_ws.h — WiFi WebSocket server for dash-hosted Studio (issue #1105)
//
// Exposes the USB JSON-lines protocol over a WebSocket so a browser-based
// Studio (no raw TCP access) can drive the dash. Mirrors the contract of
// wifi_tcp (#1071/#1073): identical JSON payloads, single client at a time,
// commands flow through UsbComm::handleLine() and proactive telemetry rides
// the same aux-sink fan-out.
//
// Endpoint: ws://canshift.local:81/  (or ws://<dash-ip>:81/)
//
// Port 81 instead of 80 because the chosen library
// (Links2004/arduinoWebSockets) listens on its own WiFiServer rather than
// sharing the Arduino WebServer instance used for the OTA HTTP POST on port
// 80. Documented in the firmware README "Wi-Fi" section. mDNS advertises the
// WS service alongside the TCP one so Studio discovery works the same way.
//
// Lifecycle: start()/stop() are called from WifiAp::start()/stop() — the WS
// server only runs while the AP is up, just like the TCP server. WS frames
// are text-only; each frame is exactly one JSON object (no '\n' terminator —
// the WS frame boundary replaces it). Dispatch path through
// UsbComm::handleLine() is identical so command semantics are byte-for-byte
// equivalent to USB / TCP.
//
// Concurrency: single client. A second connection is immediately disconnected
// on its WStype_CONNECTED event. The TCP server (#1073) still runs in
// parallel — both transports can mirror telemetry to their own client
// concurrently because the aux-sink hand-off is gated on connect / disconnect.

#include "app_config.h"

#if APP_BLE_ENABLED && APP_WIFI_OTA_ENABLED

namespace WifiWsServer {

/** Start the WS server task on port 81. No-op if already active. */
void start();

/** Signal the server task to stop. The task closes the listener and exits. */
void stop();

/** Returns true if the server task is currently running. */
bool isActive();

/** Returns true if a client is currently connected. */
bool hasClient();

} // namespace WifiWsServer

#endif // APP_BLE_ENABLED && APP_WIFI_OTA_ENABLED
