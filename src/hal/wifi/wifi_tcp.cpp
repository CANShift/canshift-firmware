// wifi_tcp.cpp — WiFi TCP server for Studio connection (issue #1071)
//
// Started from WifiAp::start() once the AP and mDNS responder are up.
// Single-client policy: a second concurrent connection is rejected at
// accept() time so the wire protocol stays line-ordered without locking.
// Lines are dispatched through UsbComm::handleLine() with a TCP-write sink
// so command replies come back over the same socket. Proactive telemetry
// is mirrored via UsbComm::setAuxSink while a client is connected.

#include "app_config.h"

#if APP_BLE_ENABLED && APP_WIFI_OTA_ENABLED

    #include "wifi_tcp.h"
    #include "hal/usb/usb_comm.h"
    #include "diag/logger.h"

    #include <Arduino.h>
    #include <WiFi.h>
    #include <esp_heap_caps.h>
    #include <esp_task_wdt.h>
    #include <freertos/FreeRTOS.h>
    #include <freertos/task.h>
    #include <stdint.h>
    #include <stdlib.h>
    #include <string.h>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

namespace {

constexpr uint16_t TCP_PORT = 5050;

// Idle delay between accept() / available() polls. 10 ms keeps latency
// reasonable for command dispatch without starving lower-priority tasks
// (sim, BLE) on core 1.
constexpr TickType_t TCP_TICK_DELAY = pdMS_TO_TICKS(10);

// Max bytes to drain from the socket per tick. Each tick parses up to one
// full line; reading more would risk holding the WiFi stack past the WDT
// tolerance on a chatty client. The line buffer is sized via USB_RX_BUF_SIZE
// already, so a single PUT_CONFIG burn (~13 KB) takes <130 ticks (~1.3 s) to
// drain — bounded and well under TASK_WDT_TIMEOUT_MS.
constexpr size_t MAX_READ_PER_TICK = 1024;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

WiFiServer s_server(TCP_PORT);
WiFiClient s_client;
TaskHandle_t s_taskHandle = nullptr;
volatile bool s_active = false;

// Line-assembly buffer for inbound bytes. Sized identically to the USB RX
// buffer so the largest CMD_PUT_CONFIG payload (~13 KB at schema v1.11) fits
// in one line. Heap-allocated in start() so the ~16 KB is only consumed
// while the AP+TCP server is actually running (BSS-reserving it would push
// the always-on footprint over the ESP32's 320 KB DRAM budget once NimBLE
// and LVGL have claimed their share). Issue #1071.
char *s_lineBuf = nullptr;
size_t s_linePos = 0;

// ---------------------------------------------------------------------------
// Output sink — pushes a wire-protocol line out through the live client.
// Called both from UsbComm::handleLine (command reply path) and from the
// proactive telemetry path via the auxiliary-sink registration.
// ---------------------------------------------------------------------------
void tcpWriteSink(const char *data, size_t len) {
    if (!data || len == 0)
        return;
    if (!s_client || !s_client.connected())
        return;
    s_client.write(reinterpret_cast<const uint8_t *>(data), len);
    if (data[len - 1] != '\n') {
        const uint8_t nl = '\n';
        s_client.write(&nl, 1);
    }
}

// Drop the line buffer back to empty. Called on new-client accept (so a
// half-line left over from a previous client never crosses session
// boundaries) and on disconnect.
void resetLineBuffer() {
    s_linePos = 0;
    if (s_lineBuf)
        s_lineBuf[0] = '\0';
}

// Disconnect the current client cleanly: clear the aux sink so telemetry
// stops mirroring, stop the underlying socket, reset the line buffer.
void dropClient(const char *reason) {
    if (s_client) {
        LOG_INFO("WiFiTCP", "Client disconnected (%s)", reason ? reason : "?");
        s_client.stop();
    }
    UsbComm::setAuxSink(nullptr);
    resetLineBuffer();
}

// Accept a new pending client iff none is connected. Mirrors the
// single-client contract from issue #1071: any second connect attempt is
// closed immediately so the wire stays serialised to one peer.
void acceptOrReject() {
    if (!s_server.hasClient())
        return;
    WiFiClient incoming = s_server.available();
    if (!incoming)
        return;
    if (s_client && s_client.connected()) {
        LOG_WARN("WiFiTCP", "Refusing second client — single-client policy");
        incoming.stop();
        return;
    }
    s_client = incoming;
    s_client.setNoDelay(true);
    resetLineBuffer();
    UsbComm::setAuxSink(&tcpWriteSink);
    LOG_INFO("WiFiTCP", "Client connected on port %u", static_cast<unsigned>(TCP_PORT));
}

// Drain available bytes from the socket into the line buffer, dispatching
// every complete '\n'-terminated line via UsbComm::handleLine. Bounded at
// MAX_READ_PER_TICK bytes per call so the WiFi RX path cannot starve the
// rest of the task loop (WDT feed, accept polling).
void pumpInbound() {
    if (!s_client || !s_client.connected())
        return;
    if (!s_lineBuf)
        return; // line buffer alloc failed at start() — bytes drop on the floor
    size_t read = 0;
    while (read < MAX_READ_PER_TICK && s_client.available() > 0) {
        const int b = s_client.read();
        if (b < 0)
            break;
        ++read;
        const char c = static_cast<char>(b);
        if (c == '\n') {
            if (s_linePos > 0) {
                s_lineBuf[s_linePos] = '\0';
                UsbComm::handleLine(s_lineBuf, s_linePos, &tcpWriteSink);
            }
            s_linePos = 0;
        } else if (s_linePos < USB_RX_BUF_SIZE - 1) {
            s_lineBuf[s_linePos++] = c;
        } else {
            LOG_WARN("WiFiTCP", "Line buffer overflow — dropping packet");
            s_linePos = 0;
        }
    }
}

// ---------------------------------------------------------------------------
// Task body
// ---------------------------------------------------------------------------
void tcpTaskFn(void *) {
    // Subscribe to the Task WDT so a hang in the TCP polling loop trips
    // the panic handler the same way every other long-lived task does.
    // Skipped in simulation builds — the WDT is itself disabled there
    // (BootSequence) and esp_task_wdt_add would return ESP_ERR_INVALID_STATE.
    const esp_err_t wdtAddErr = esp_task_wdt_add(nullptr);
    if (wdtAddErr != ESP_OK) {
        LOG_WARN("WiFiTCP", "WDT add(wifi_tcp) failed: %d", static_cast<int>(wdtAddErr));
    }

    // Heap-allocate the line buffer here (not in start(), which runs on the
    // caller's task) so the largest contiguous block on this core's heap is
    // measured at the right moment. ~16 KB succeeds on a healthy boot but is
    // not a given on a fragmented runtime — fail soft so the AP still serves
    // /status + /ota even if the JSON-lines transport can't come up.
    s_lineBuf = static_cast<char *>(
        heap_caps_malloc(USB_RX_BUF_SIZE, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
    if (s_lineBuf) {
        s_lineBuf[0] = '\0';
    } else {
        LOG_ERROR("WiFiTCP", "Line buffer alloc (%u B) failed — TCP server degraded",
                  static_cast<unsigned>(USB_RX_BUF_SIZE));
    }
    s_linePos = 0;

    s_server.begin();
    s_server.setNoDelay(true);
    LOG_INFO("WiFiTCP", "Listening on TCP/%u", static_cast<unsigned>(TCP_PORT));

    while (s_active) {
        acceptOrReject();

        if (s_client && !s_client.connected()) {
            dropClient("peer closed");
        } else {
            pumpInbound();
        }

        esp_task_wdt_reset();
        vTaskDelay(TCP_TICK_DELAY);
    }

    // Teardown — drop the client first so the aux sink is cleared before
    // the listener stops accepting.
    dropClient("server stop");
    s_server.stop();

    // Release the line buffer back to the heap so the ~16 KB doesn't squat
    // on DRAM between AP sessions.
    if (s_lineBuf) {
        heap_caps_free(s_lineBuf);
        s_lineBuf = nullptr;
    }
    s_linePos = 0;

    const esp_err_t wdtDelErr = esp_task_wdt_delete(nullptr);
    if (wdtDelErr != ESP_OK) {
        LOG_WARN("WiFiTCP", "WDT delete(wifi_tcp) failed: %d", static_cast<int>(wdtDelErr));
    }

    s_taskHandle = nullptr;
    LOG_INFO("WiFiTCP", "Server stopped");
    vTaskDelete(nullptr);
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void WifiTcpServer::start() {
    if (s_active)
        return;
    s_active = true;
    // Core 1 + priority 5 + 4 KB stack (TASK_*_WIFI_TCP — see app_config.h).
    // Pinned to core 1 to live alongside the WiFi AP HTTP server task, which
    // shares the Arduino-WiFi stack and is itself on core 1.
    xTaskCreatePinnedToCore(tcpTaskFn, "wifi_tcp", TASK_STACK_WIFI_TCP, nullptr, TASK_PRIO_WIFI_TCP,
                            &s_taskHandle, TASK_CORE_WIFI_TCP);
}

void WifiTcpServer::stop() {
    s_active = false;
}

bool WifiTcpServer::isActive() {
    return s_active;
}

bool WifiTcpServer::hasClient() {
    return s_client && s_client.connected();
}

#endif // APP_BLE_ENABLED && APP_WIFI_OTA_ENABLED
