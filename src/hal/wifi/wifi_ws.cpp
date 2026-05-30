// wifi_ws.cpp — WiFi WebSocket server for dash-hosted Studio (issue #1105)
//
// Started from WifiAp::start() after the AP, mDNS and TCP server are up.
// Single-client policy mirrors wifi_tcp: a second concurrent connection is
// disconnected on its WStype_CONNECTED event so the wire protocol stays
// line-ordered without locking. Each inbound text frame is one complete JSON
// object (NO trailing '\n' — WS framing replaces it). Frames are dispatched
// through UsbComm::handleLine() with a WS-write sink so command responses
// come back over the same socket. Proactive telemetry is mirrored via the
// existing aux-sink mechanism — but only while the TCP transport isn't
// already claiming the slot, so WS and TCP can coexist.

#include "app_config.h"

#if APP_BLE_ENABLED && APP_WIFI_OTA_ENABLED

    // Trim the library's compile-time client array to 2: one for the legitimate
    // connection, one head-room slot so the library can ACCEPT a second
    // attempt and we can disconnect() it with an explanatory event instead of
    // letting the kernel quietly drop the SYN. Each WSclient_t carries a TCP
    // client handle plus header-state buffers (~120 B + Arduino String backing
    // store); capping at 2 reclaims most of the ~600 B the default 5-slot
    // array would otherwise burn in BSS.
    #ifndef WEBSOCKETS_SERVER_CLIENT_MAX
        #define WEBSOCKETS_SERVER_CLIENT_MAX 2
    #endif

    #include "wifi_ws.h"
    #include "hal/usb/usb_comm.h"
    #include "diag/logger.h"

    #include <Arduino.h>
    #include <WiFi.h>
    #include <WebSocketsServer.h>
    #include <esp_heap_caps.h>
    #include <esp_task_wdt.h>
    #include <freertos/FreeRTOS.h>
    #include <freertos/task.h>
    #include <stdint.h>
    #include <string.h>

namespace {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

// Port 81 — the chosen library (Links2004/arduinoWebSockets) opens its own
// listening socket via WiFiServer; it cannot share the WebServer instance on
// port 80 that already serves /status and /ota. Documented as the WS endpoint
// in the firmware README so Studio knows where to dial.
constexpr uint16_t WS_PORT = 81;

// Idle delay between WS loop() calls. 10 ms matches the TCP server cadence
// for command-dispatch latency without starving lower-priority tasks (sim,
// BLE) sharing core 1.
constexpr TickType_t WS_TICK_DELAY = pdMS_TO_TICKS(10);

// Sentinel for "no client connected". WSclient_t indices are uint8_t, so
// 0xFF is safely outside the valid range capped at WEBSOCKETS_SERVER_CLIENT_MAX.
constexpr uint8_t WS_INVALID_CLIENT = 0xFF;

// CLOSE-frame reason sent when refusing a second concurrent client. Short
// because some clients display it verbatim and the library copies it into
// a temporary buffer before the frame goes out.
constexpr char WS_REFUSE_REASON[] = "single-client only";

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

WebSocketsServer s_ws(WS_PORT);
TaskHandle_t s_taskHandle = nullptr;
volatile bool s_active = false;

// Tracks the single accepted client. WS_INVALID_CLIENT means no peer.
volatile uint8_t s_clientNum = WS_INVALID_CLIENT;

// True while we hold the UsbComm aux-sink registration. We only claim the
// slot if it was free at connect time (TCP gets priority because it landed
// first), and we only release it if we still own it — never stomping a
// later registration. Issue #1105.
bool s_ownsAuxSink = false;

// ---------------------------------------------------------------------------
// Output sink — pushes a wire-protocol line out to the live WS client as a
// single TEXT frame. Called both from UsbComm::handleLine (command-reply
// path) and from the proactive telemetry path via the aux-sink registration.
// The trailing '\n' that USB / TCP transports carry is stripped here: the
// WS frame boundary replaces it (per #1105 contract). Stripping rather than
// duplicating keeps the JSON parseable on browsers that disallow trailing
// whitespace in a single frame.
// ---------------------------------------------------------------------------
void wsWriteSink(const char *data, size_t len) {
    if (!data || len == 0)
        return;
    if (s_clientNum == WS_INVALID_CLIENT)
        return;

    size_t writeLen = len;
    if (writeLen > 0 && data[writeLen - 1] == '\n') {
        --writeLen;
        if (writeLen == 0)
            return;
    }
    s_ws.sendTXT(s_clientNum, reinterpret_cast<const uint8_t *>(data), writeLen);
}

// Claim the UsbComm aux sink iff nobody else holds it. Returns true on
// success so the caller knows whether to release on disconnect.
bool tryClaimAuxSink() {
    if (UsbComm::hasAuxSink())
        return false;
    UsbComm::setAuxSink(&wsWriteSink);
    return true;
}

// Release the aux sink iff we previously claimed it. Idempotent — safe to
// call on every disconnect path including the dangling teardown in stop().
void releaseAuxSinkIfOwned() {
    if (!s_ownsAuxSink)
        return;
    UsbComm::setAuxSink(nullptr);
    s_ownsAuxSink = false;
}

// Drop the active client (server-initiated). Used both for the
// single-client refusal path and during stop()/teardown.
void disconnectClient(uint8_t num, const char *reason) {
    LOG_INFO("WiFiWS", "Client %u disconnected (%s)", static_cast<unsigned>(num),
             reason ? reason : "?");
    s_ws.disconnect(num);
}

// ---------------------------------------------------------------------------
// WS event handler — runs on the WS task while inside s_ws.loop().
// ---------------------------------------------------------------------------
void onWsEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED: {
            // Single-client refusal: if we already track a client AND that client
            // is still in the table, drop the newcomer. The library calls back
            // synchronously on disconnect so s_clientNum is stale-safe to compare.
            if (s_clientNum != WS_INVALID_CLIENT && s_clientNum != num) {
                LOG_WARN("WiFiWS", "Refusing second client %u — single-client policy",
                         static_cast<unsigned>(num));
                // sendTXT first so the client sees a parseable reason before the
                // close. This is best-effort: a fast peer may FIN before the
                // frame flushes. The kernel-level close still follows.
                s_ws.sendTXT(num, reinterpret_cast<const uint8_t *>(WS_REFUSE_REASON),
                             sizeof(WS_REFUSE_REASON) - 1);
                s_ws.disconnect(num);
                return;
            }
            s_clientNum = num;
            s_ownsAuxSink = tryClaimAuxSink();
            LOG_INFO("WiFiWS", "Client %u connected on port %u (aux sink: %s)",
                     static_cast<unsigned>(num), static_cast<unsigned>(WS_PORT),
                     s_ownsAuxSink ? "ws" : "shared-with-tcp");
            break;
        }

        case WStype_DISCONNECTED: {
            if (s_clientNum == num) {
                LOG_INFO("WiFiWS", "Client %u disconnected", static_cast<unsigned>(num));
                releaseAuxSinkIfOwned();
                s_clientNum = WS_INVALID_CLIENT;
            }
            break;
        }

        case WStype_TEXT: {
            if (s_clientNum != num) {
                // Refused-but-still-talking client — drop the frame. The
                // matching disconnect event will tear it down momentarily.
                return;
            }
            if (!payload || length == 0)
                return;
            // The library guarantees a NUL terminator one byte past `length` for
            // text frames (see WebSockets::messageReceived). UsbComm::handleLine
            // expects a NUL-terminated buffer, so passing payload directly is
            // safe without an extra copy.
            UsbComm::handleLine(reinterpret_cast<const char *>(payload), length, &wsWriteSink);
            break;
        }

        case WStype_ERROR: {
            LOG_WARN("WiFiWS", "WS error on client %u (len=%u)", static_cast<unsigned>(num),
                     static_cast<unsigned>(length));
            break;
        }

        case WStype_PING:
        case WStype_PONG:
            // Library handles the protocol-level reply automatically; no app work.
            break;

        default:
            // BIN / FRAGMENT_* are unused — Studio sends TEXT frames only per
            // the #1105 contract. Silently ignore so a misbehaving peer doesn't
            // spam the logger.
            break;
    }
}

// ---------------------------------------------------------------------------
// Task body
// ---------------------------------------------------------------------------
void wsTaskFn(void *) {
    // Subscribe to the Task WDT so a hang inside s_ws.loop() trips the panic
    // handler the same way every other long-lived task does. Skipped in
    // simulation builds — the WDT is itself disabled there (BootSequence) and
    // esp_task_wdt_add would return ESP_ERR_INVALID_STATE.
    const esp_err_t wdtAddErr = esp_task_wdt_add(nullptr);
    if (wdtAddErr != ESP_OK) {
        LOG_WARN("WiFiWS", "WDT add(wifi_ws) failed: %d", static_cast<int>(wdtAddErr));
    }

    s_clientNum = WS_INVALID_CLIENT;
    s_ownsAuxSink = false;

    s_ws.onEvent(&onWsEvent);
    s_ws.begin();
    LOG_INFO("WiFiWS", "Listening on WS/%u (path /)", static_cast<unsigned>(WS_PORT));

    while (s_active) {
        s_ws.loop();

        esp_task_wdt_reset();
        vTaskDelay(WS_TICK_DELAY);
    }

    // Teardown — release the aux sink first so telemetry stops mirroring
    // before the listener closes, then drop any live client cleanly so the
    // peer sees a CLOSE frame instead of a half-open socket.
    releaseAuxSinkIfOwned();
    if (s_clientNum != WS_INVALID_CLIENT) {
        disconnectClient(s_clientNum, "server stop");
        s_clientNum = WS_INVALID_CLIENT;
    }
    s_ws.close();

    const esp_err_t wdtDelErr = esp_task_wdt_delete(nullptr);
    if (wdtDelErr != ESP_OK) {
        LOG_WARN("WiFiWS", "WDT delete(wifi_ws) failed: %d", static_cast<int>(wdtDelErr));
    }

    s_taskHandle = nullptr;
    LOG_INFO("WiFiWS", "Server stopped");
    vTaskDelete(nullptr);
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void WifiWsServer::start() {
    if (s_active)
        return;
    s_active = true;
    // Core 1 + priority 5 + 4 KB stack (TASK_*_WIFI_WS — see app_config.h).
    // Pinned to core 1 to live alongside the WiFi AP HTTP server and TCP
    // server tasks; all three share the Arduino-WiFi stack on core 1.
    xTaskCreatePinnedToCore(wsTaskFn, "wifi_ws", TASK_STACK_WIFI_WS, nullptr, TASK_PRIO_WIFI_WS,
                            &s_taskHandle, TASK_CORE_WIFI_WS);
}

void WifiWsServer::stop() {
    s_active = false;
}

bool WifiWsServer::isActive() {
    return s_active;
}

bool WifiWsServer::hasClient() {
    return s_clientNum != WS_INVALID_CLIENT;
}

#endif // APP_BLE_ENABLED && APP_WIFI_OTA_ENABLED
