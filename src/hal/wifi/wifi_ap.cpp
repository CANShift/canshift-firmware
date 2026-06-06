// wifi_ap.cpp — WiFi AP mode orchestrator.
//
// Behind APP_WIFI_OTA_ENABLED so the Arduino WiFi / WebServer / Update libs
// stay out of the link when not needed (~80 KB flash). When disabled, the
// WifiAp:: API resolves to no-op stubs so BLE callers don't need to know.
//
// Split into two TUs during the #1207 refactor:
//   - wifi_ap.cpp     (this file) — AP lifecycle: SSID build, password
//                                   persistence, the apTaskFn FreeRTOS task
//                                   that brings up softAP / mDNS / TCP / WS,
//                                   drives the WebServer event loop, and
//                                   tears everything down. Owns the public
//                                   WifiAp:: API.
//   - wifi_ap_ota.cpp — HTTP server (WebServer instance), every endpoint
//                       it registers (/status, /ota, dash SPA assets), and
//                       the OTA HMAC / bearer-token / Update.begin sticky
//                       state surfaced in handleOtaComplete.
//
// The orchestrator drives the OTA module through the WifiApInternal::*
// lifecycle hooks declared in wifi_ap_internal.h.

#include "app_config.h"
#if APP_BLE_ENABLED

    #include "wifi_ap.h"
    #include "diag/logger.h"

    #if APP_WIFI_OTA_ENABLED

        #include "hal/wifi/wifi_ap_internal.h"
        #include "hal/wifi/wifi_tcp.h"
        #include "hal/wifi/wifi_ws.h"

        #include <WiFi.h>
        #include <Arduino.h>
        #include <ESPmDNS.h>
        #include <Preferences.h>
        #include <esp_heap_caps.h>
        #include <esp_system.h>
        #include <esp_task_wdt.h>
        #include <freertos/FreeRTOS.h>
        #include <freertos/task.h>
        #include <stdio.h>
        #include <string.h>

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static TaskHandle_t s_taskHandle = nullptr;
static volatile bool s_active = false;
static char s_ssid[20] = {};

// AP password — fixed default "canshift" (8 chars, WPA2 minimum). Was
// per-device 32-char random hex (#910 — 128 bits entropy) but ergonomically
// hostile (manual entry on a phone keypad). Threat model trade-off: the AP
// is only active when the user explicitly toggles WiFi auto-start, and the
// dash is a passenger-cabin device — the attacker would need to be inside
// the car or within ~5 m. If you redeploy in a more exposed environment,
// flip `AP_PASSWORD_USE_RANDOM` to 1 to restore the per-device random
// secret (and surface it via Studio so the user can read it).
static constexpr bool AP_PASSWORD_USE_RANDOM = false;
static constexpr char AP_PASSWORD_DEFAULT[] = "canshift";
// Buffer capacity — holds either the default or a 32-hex random secret.
static constexpr size_t AP_PASSWORD_LEN = 32;
// 128 bits, hex-encoded (random path only).
static constexpr size_t AP_PASSWORD_ENTROPY_BYTES = 16;
static char s_password[AP_PASSWORD_LEN + 1] = {};

static constexpr char NVS_NS_WIFI_AP[] = "wifi_ap";
static constexpr char NVS_KEY_PWD[] = "pwd";
// Persistent "auto-start AP on boot" toggle (#1077 audit blocker #3). Stored
// as uint8 (0/1) so a fresh device (no NVS entry) reads back as 0 — AP stays
// dormant until the user opts in via the Settings page or via the BLE
// `start_wifi_ap` CMD path. When 1, BootSequence::run() brings the AP up
// after USB init AND the apTaskFn loop ignores BLE_WIFI_AP_TIMEOUT_MS so the
// dash-hosted Studio path stays reachable indefinitely.
static constexpr char NVS_KEY_AUTO[] = "auto";

// ---------------------------------------------------------------------------
// AP password lifecycle
// ---------------------------------------------------------------------------

namespace {

void buildSsid() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(s_ssid, sizeof(s_ssid), "CANShift-%02X%02X", mac[4], mac[5]);
}

void ensurePassword() {
    if (s_password[0] != '\0')
        return; // already loaded this boot

    if (!AP_PASSWORD_USE_RANDOM) {
        // Fixed default for ergonomics — see AP_PASSWORD_DEFAULT note above.
        strlcpy(s_password, AP_PASSWORD_DEFAULT, sizeof(s_password));
        return;
    }

    Preferences p;
    if (p.begin(NVS_NS_WIFI_AP, /*readOnly=*/true)) {
        const size_t len = p.getString(NVS_KEY_PWD, s_password, sizeof(s_password));
        p.end();
        if (len == AP_PASSWORD_LEN)
            return; // valid persisted value
    }

    // Missing, empty, or wrong length — generate a fresh 128-bit random
    // password. `esp_fill_random` populates the buffer from the hardware
    // RNG in a single call (cheaper than two `esp_random()` calls and avoids
    // the per-word join). Hex-encoded for a 32-char ASCII string.
    uint8_t entropy[AP_PASSWORD_ENTROPY_BYTES];
    esp_fill_random(entropy, sizeof(entropy));
    for (size_t i = 0; i < AP_PASSWORD_ENTROPY_BYTES; ++i) {
        snprintf(&s_password[i * 2], 3, "%02x", entropy[i]);
    }

    Preferences pw;
    if (pw.begin(NVS_NS_WIFI_AP, /*readOnly=*/false)) {
        pw.putString(NVS_KEY_PWD, s_password);
        pw.end();
        LOG_INFO("WiFi", "Generated new AP password (persisted in NVS)");
    } else {
        LOG_WARN("WiFi", "NVS open failed — using volatile AP password");
    }
}

void apTaskFn(void *) {
    // Issue #1006 — subscribe this task to the WDT armed in BootSequence.
    // The task is created on-demand from WifiAp::start() and self-deletes
    // after BLE_WIFI_AP_TIMEOUT_MS or an explicit stop. Subscribe from
    // inside the task body (pass nullptr = current task) so the handle slot
    // is guaranteed live when esp_task_wdt_add runs.
    //
    // Skipped in simulation builds — the WDT is itself disabled there
    // (boot_sequence.cpp) and esp_task_wdt_add would return ESP_ERR_INVALID_STATE.
    const esp_err_t wdtAddErr = esp_task_wdt_add(nullptr);
    if (wdtAddErr != ESP_OK) {
        LOG_WARN("WiFi", "WDT add(wifi_ap) failed: %d", static_cast<int>(wdtAddErr));
    }

    WiFi.softAP(s_ssid, s_password);
    LOG_INFO("WiFi", "AP started — SSID: %s  IP: %s", s_ssid, WiFi.softAPIP().toString().c_str());

    // mDNS responder — exposes the dash as `canshift.local` and advertises
    // the JSON-lines TCP server as `_canshift._tcp` so Studio can discover
    // and connect without a manual IP. Failure is non-fatal: the AP keeps
    // serving and a manual IP fallback still works. Issue #1071.
    //
    // The WebSocket transport (#1105) ships as a sibling `_canshift_ws._tcp`
    // service on port 81 because the chosen WS library opens its own
    // listening socket rather than sharing port 80 with the OTA WebServer.
    // The `path=/` TXT record disambiguates from the TCP service for
    // dash-hosted Studio which selects on path.
    if (MDNS.begin("canshift")) {
        MDNS.addService("canshift", "tcp", 5050);
        MDNS.addService("canshift_ws", "tcp", 81);
        MDNS.addServiceTxt("canshift_ws", "tcp", "path", "/");
        LOG_INFO("WiFi", "mDNS up: canshift.local  services: _canshift._tcp:5050 "
                         "_canshift_ws._tcp:81");
    } else {
        LOG_WARN("WiFi", "mDNS.begin() failed — Studio discovery disabled, manual IP only");
    }

    // TCP server for Studio JSON-lines transport (issue #1071). Starts after
    // the AP is live so the listening socket binds to the softAP interface.
    WifiTcpServer::start();

    // WebSocket server for browser-based dash-hosted Studio (issue #1105).
    // Mirrors the JSON-lines protocol over WS text frames on port 81. Single
    // client; coexists with the TCP server above (last-connect wins the aux
    // sink slot for telemetry fan-out).
    WifiWsServer::start();

    // Derive the per-device OTA bearer token once per AP session. Cheap
    // (one SHA-256) and deterministic, so the token stays stable across
    // reboots as long as the AP password in NVS doesn't rotate.
    if (!WifiApInternal::deriveOtaToken()) {
        LOG_ERROR("WiFi", "OTA bearer token derivation failed — /ota will reject everything");
    }

    WifiApInternal::registerOtaRoutes();
    WifiApInternal::beginServer();

    // Persistent auto-start opts out of the 5-minute safety timeout (#1077
    // audit blocker #3). Rationale: the timeout exists so a one-off BLE-CMD
    // start_wifi_ap leaves no lingering RF surface / battery draw if the
    // user forgets to stop the AP. When the user has explicitly toggled
    // the persistent AP preference ON from the on-device Settings page,
    // they want the dash-hosted Studio reachable indefinitely; auto-stop
    // would silently kill that contract. Read once at task entry — the
    // preference can't flip mid-session without going through
    // setAutoStartEnabled() (which also routes through start/stop).
    const bool persistOn = WifiAp::isAutoStartEnabled();
    const uint32_t startMs = millis();
    while (s_active && (persistOn || (millis() - startMs < BLE_WIFI_AP_TIMEOUT_MS))) {
        WifiApInternal::handleClientTick();

        // Issue #1006 — WiFi AP task WDT feed. Placed AFTER handleClient()
        // so a real hang inside the OTA upload / Update.end() path still
        // trips the watchdog. The 10 ms vTaskDelay cadence + Update.end()'s
        // worst-case ~2 s erase-and-finalise stays comfortably below
        // TASK_WDT_TIMEOUT_MS (8 s).
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Tear down the WS server first (issue #1105) and then the TCP server
    // (issue #1071) before dropping the AP so each listener socket releases
    // its port and any connected Studio sees a clean CLOSE / FIN rather than
    // a half-open connection. WS goes first because it shares the same
    // Arduino-WiFi stack as TCP and its teardown is slightly slower (the
    // library walks WEBSOCKETS_SERVER_CLIENT_MAX slots).
    WifiWsServer::stop();
    WifiTcpServer::stop();
    MDNS.end();

    WifiApInternal::stopServer();
    WiFi.softAPdisconnect(true);

    // Unregister BEFORE self-delete: a deleted task that's still on the WDT
    // registry triggers the panic handler on the next watchdog tick because
    // the TCB pointer is no longer valid.
    const esp_err_t wdtDelErr = esp_task_wdt_delete(nullptr);
    if (wdtDelErr != ESP_OK) {
        LOG_WARN("WiFi", "WDT delete(wifi_ap) failed: %d", static_cast<int>(wdtDelErr));
    }

    s_active = false;
    s_taskHandle = nullptr;
    LOG_INFO("WiFi", "AP stopped");
    vTaskDelete(nullptr);
}

} // namespace

// ---------------------------------------------------------------------------
// Cross-module accessor — wifi_ap_ota.cpp reads the live password at AP
// start to derive the bearer token. Returns the same pointer ensurePassword
// populates; valid for the full AP session.
// ---------------------------------------------------------------------------

const char *WifiApInternal::apPassword() {
    return s_password;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void WifiAp::start() {
    if (s_active)
        return;
    buildSsid();      // build SSID before task starts so getSsid() is valid immediately
    ensurePassword(); // ditto for getPassword(); persists to NVS on first boot
    s_active = true;
    // Core 1, priority 5, stack 4096 B (see TASK_CORE_WIFI/PRIO_WIFI/STACK_WIFI in app_config.h).
    // On-demand task started only for OTA — runs co-resident with UI on core 1
    // because WiFi softAP needs the same Arduino-WiFi stack as the AP HTTP server.
    xTaskCreatePinnedToCore(apTaskFn, "wifi_ap", TASK_STACK_WIFI, nullptr, TASK_PRIO_WIFI,
                            &s_taskHandle, TASK_CORE_WIFI);
}

void WifiAp::stop() {
    s_active = false;
}

bool WifiAp::isActive() {
    return s_active;
}

const char *WifiAp::getSsid() {
    return s_ssid;
}

const char *WifiAp::getPassword() {
    ensurePassword(); // safe lazy fallback if start() hasn't run yet
    return s_password;
}

bool WifiAp::isAutoStartEnabled() {
    Preferences p;
    if (!p.begin(NVS_NS_WIFI_AP, /*readOnly=*/true)) {
        return false; // missing namespace = never opted in
    }
    const uint8_t v = p.getUChar(NVS_KEY_AUTO, 0);
    p.end();
    return v != 0;
}

void WifiAp::setAutoStartEnabled(bool enabled) {
    Preferences p;
    if (p.begin(NVS_NS_WIFI_AP, /*readOnly=*/false)) {
        p.putUChar(NVS_KEY_AUTO, enabled ? 1 : 0);
        p.end();
        LOG_INFO("WiFi", "AP auto-start preference: %s", enabled ? "ON" : "OFF");
    } else {
        LOG_WARN("WiFi", "NVS open failed — AP auto-start preference not persisted");
    }
    // Mirror the new preference into runtime state immediately so the user
    // sees the AP come up (or drop) the moment they toggle the Settings row,
    // without having to reboot the dash.
    if (enabled) {
        if (!s_active) {
            WifiAp::start();
        }
    } else {
        if (s_active) {
            WifiAp::stop();
        }
    }
}

void WifiAp::tickAutoStart() {
    // Minimum largest free internal block before we'll bring the AP up. Mirrors
    // the original boot-time guard introduced in #1260 to prevent WebServer
    // bad_alloc during the busiest stretch of init — only here it's polled
    // until the heap settles instead of refused once.
    constexpr size_t WIFI_AP_MIN_HEAP_BYTES = 24 * 1024;
    // Retry cadence — fast enough that the user sees the SSID within a few
    // seconds of boot once the heap has relaxed, slow enough that we don't
    // spam the logs or pay the NVS read every UI tick.
    constexpr uint32_t AUTO_START_RETRY_INTERVAL_MS = 1000;

    if (s_active)
        return;

    static uint32_t lastAttemptMs = 0;
    const uint32_t now = millis();
    if (now - lastAttemptMs < AUTO_START_RETRY_INTERVAL_MS)
        return;
    lastAttemptMs = now;

    if (!isAutoStartEnabled())
        return;

    const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    if (largest < WIFI_AP_MIN_HEAP_BYTES) {
        LOG_DEBUG("WiFi", "AP auto-start deferred — largest=%u below %u",
                  static_cast<unsigned>(largest), static_cast<unsigned>(WIFI_AP_MIN_HEAP_BYTES));
        return;
    }
    LOG_INFO("WiFi", "AP auto-start: heap settled (largest=%u) — bringing AP up",
             static_cast<unsigned>(largest));
    start();
}

    #else // !APP_WIFI_OTA_ENABLED — stubs

void WifiAp::start() {
    LOG_WARN("WiFi", "WiFi OTA disabled at compile time (APP_WIFI_OTA_ENABLED=0)");
}
void WifiAp::stop() {}
bool WifiAp::isActive() {
    return false;
}
const char *WifiAp::getSsid() {
    return "";
}
const char *WifiAp::getPassword() {
    return "";
}
bool WifiAp::isAutoStartEnabled() {
    return false;
}
void WifiAp::setAutoStartEnabled(bool /*enabled*/) {
    LOG_WARN("WiFi", "WiFi OTA disabled at compile time — AP auto-start ignored");
}
void WifiAp::tickAutoStart() {}

    #endif // APP_WIFI_OTA_ENABLED

#endif // APP_BLE_ENABLED
