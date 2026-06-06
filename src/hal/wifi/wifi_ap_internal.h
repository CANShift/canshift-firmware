#pragma once
// wifi_ap_internal.h — Cross-module forward decls for the WiFi AP layer.
// Carved out of wifi_ap.cpp during the #1207 refactor. Two translation units
// share this header:
//
//   - wifi_ap.cpp     — orchestrator + AP lifecycle: SSID build, password
//                       persistence, the apTaskFn FreeRTOS task that brings
//                       up softAP / mDNS / TCP / WS, drives the WebServer
//                       run loop, and tears everything down on stop. Owns
//                       the public WifiAp:: API (signatures byte-identical
//                       to pre-refactor).
//   - wifi_ap_ota.cpp — HTTP server (WebServer instance) + every endpoint
//                       it registers: /status, /ota upload + complete,
//                       and (under APP_SPA_SERVE) the dash-hosted Studio
//                       SPA asset routes. Owns OTA HMAC / bearer-token /
//                       Update.begin sticky state. The orchestrator drives
//                       its lifecycle through the accessor functions
//                       declared below.
//
// The PUBLIC API in wifi_ap.h is byte-identical to pre-refactor. This header
// is internal — never include it from outside src/hal/wifi/.

#include "app_config.h"

#if APP_BLE_ENABLED && APP_WIFI_OTA_ENABLED

namespace WifiApInternal {

// ---------------------------------------------------------------------------
// AP password — owned by wifi_ap.cpp (lazy-loaded from NVS in ensurePassword).
// The OTA module reads it once at AP start to derive the per-device bearer
// token (SHA-256(password || salt)). Pointer stays valid for the full AP
// session.
// ---------------------------------------------------------------------------

const char *apPassword();

// ---------------------------------------------------------------------------
// HTTP server lifecycle — the WebServer instance lives in wifi_ap_ota.cpp
// because every route + handler that touches it is OTA / SPA-related. The
// orchestrator (wifi_ap.cpp) calls these in the apTaskFn run loop:
//
//   - registerOtaRoutes()   — wires /status, /ota, and (under APP_SPA_SERVE)
//                             the dash SPA assets. Also calls
//                             collectHeaders("Authorization") so the bearer
//                             check survives the framework's header filter.
//                             Called once after deriveOtaToken().
//   - deriveOtaToken()      — recomputes the per-device bearer token from
//                             the current AP password. Returns false only if
//                             the password is empty (we never started). Must
//                             be called before registerOtaRoutes() so the
//                             token is live when the first request lands.
//   - beginServer()         — starts the WebServer listening on port 80.
//   - handleClientTick()    — single iteration of the WebServer event loop.
//                             Called every 10 ms from apTaskFn between WDT
//                             feeds.
//   - stopServer()          — closes the listening socket. Called during AP
//                             teardown after WS / TCP / mDNS are stopped.
// ---------------------------------------------------------------------------

bool deriveOtaToken();
void registerOtaRoutes();
void beginServer();
void handleClientTick();
void stopServer();

} // namespace WifiApInternal

#endif // APP_BLE_ENABLED && APP_WIFI_OTA_ENABLED
