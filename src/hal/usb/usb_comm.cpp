// usb_comm.cpp — USB serial communication layer stub
//
// Phase 1 implementation: minimal command receiver.
// Full binary framing protocol is a TODO — see usb_comm.h for spec.

#include "usb_comm.h"
#include "board_config.h"
#include "app_config.h"
#include "diag/logger.h"
#include "hal/storage/storage_driver.h"
#include "config/config_loader.h"
#include "ui/settings_page.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_system.h>     // esp_restart()

// Forward-declared FreeRTOS LVGL mutex from main.cpp
extern SemaphoreHandle_t g_lvglMutex;

// ---------------------------------------------------------------------------
// Receive state machine
// ---------------------------------------------------------------------------

namespace {

    // Simple line-based protocol stub:
    // Desktop sends: {"cmd": 1, "payload": "..."}\n
    // Firmware responds with a JSON line.
    // TODO: Replace with binary framing protocol defined in usb_comm.h

    static char s_rxBuf[USB_RX_BUF_SIZE];
    static size_t s_rxPos = 0;

    // Serialization buffer for writing dashboard.json back to SPIFFS.
    // Sized to match CONFIG_JSON_DOC_DASHBOARD (see app_config.h).
    static char s_writeBuf[CONFIG_JSON_DOC_DASHBOARD];

    // Handle CMD_PUT_CONFIG (0x02): receive dashboard JSON from studio,
    // write to SPIFFS, then reboot so the new config takes effect cleanly.
    void handlePutConfig(const char* jsonLine) {
        // Parse the full command JSON — payload may be several KB
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, jsonLine);
        if (err) {
            LOG_WARN("USB", "PUT_CONFIG parse error: %s", err.c_str());
            Serial.println("{\"status\":\"error\",\"message\":\"parse_error\"}");
            return;
        }

        JsonObjectConst payload = doc["payload"].as<JsonObjectConst>();
        if (payload.isNull()) {
            LOG_WARN("USB", "PUT_CONFIG: missing payload field");
            Serial.println("{\"status\":\"error\",\"message\":\"missing_payload\"}");
            return;
        }

        // Serialize the payload object back to a JSON string
        size_t written = serializeJson(payload, s_writeBuf, sizeof(s_writeBuf));
        if (written == 0) {
            LOG_ERROR("USB", "PUT_CONFIG: serialization failed (buffer too small?)");
            Serial.println("{\"status\":\"error\",\"message\":\"serialize_failed\"}");
            return;
        }

        // Write new dashboard.json to SPIFFS
        bool ok = StorageDriver::writeFile(
            CONFIG_PATH_DASHBOARD,
            reinterpret_cast<const uint8_t*>(s_writeBuf),
            written);
        if (!ok) {
            LOG_ERROR("USB", "PUT_CONFIG: SPIFFS write failed");
            Serial.println("{\"status\":\"error\",\"message\":\"write_failed\"}");
            return;
        }

        LOG_INFO("USB", "PUT_CONFIG: dashboard.json updated (%u bytes) — rebooting", written);

        // Acknowledge before rebooting so the studio receives RSP_OK
        Serial.flush();
        Serial.println("{\"status\":\"ok\"}");
        Serial.flush();

        delay(150);     // Allow bytes to drain over USB before reset
        esp_restart();
    }

    void handleScreenSettings(const JsonObjectConst& obj) {
        uint8_t  brightness = obj["brightness"] | 80;
        uint8_t  contrast   = obj["contrast"]   | 50;
        uint32_t sleepS     = obj["sleep"]       | 0u;
        uint16_t rotation   = obj["rotation"]    | 0u;

        // Take LVGL mutex — SettingsPage uses LVGL objects
        if (xSemaphoreTake(g_lvglMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            SettingsPage::applyFromUsb(brightness, contrast, sleepS, rotation);
            xSemaphoreGive(g_lvglMutex);
            Serial.println("{\"rsp\":128,\"msg\":\"screen_settings_ok\"}");
        } else {
            LOG_WARN("USB", "Screen settings: could not acquire LVGL mutex");
            Serial.println("{\"rsp\":129,\"msg\":\"busy\"}");
        }
    }

    void handleCommand(const char* jsonLine) {
        LOG_DEBUG("USB", "Received command: %.40s...", jsonLine);

        // Peek at cmd with a small doc to route before allocating a large buffer.
        // For CMD_PUT_CONFIG the payload is too large for a 256-byte doc.
        StaticJsonDocument<64> peekDoc;
        deserializeJson(peekDoc, jsonLine);
        uint8_t cmd = peekDoc["cmd"] | 0;

        if (cmd == UsbComm::CMD_PUT_CONFIG) {
            handlePutConfig(jsonLine);
            return;
        }

        // All other commands fit in a small document
        StaticJsonDocument<512> doc;
        DeserializationError err = deserializeJson(doc, jsonLine);
        if (err) {
            LOG_WARN("USB", "JSON parse error: %s", err.c_str());
            Serial.println("{\"status\":\"error\",\"message\":\"parse_error\"}");
            return;
        }

        switch (cmd) {
            case UsbComm::CMD_SCREEN_SETTINGS:
                handleScreenSettings(doc.as<JsonObjectConst>());
                break;
            default:
                LOG_DEBUG("USB", "Unhandled cmd: 0x%02X", cmd);
                Serial.println("{\"status\":\"ok\"}");
                break;
        }
    }

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void UsbComm::init() {
    s_rxPos = 0;
    memset(s_rxBuf, 0, sizeof(s_rxBuf));
    LOG_INFO("USB", "USB comm initialized (phase 1 stub)");
}

void UsbComm::tick() {
    // Read available bytes from Serial (UART0 / USB bridge)
    while (Serial.available() > 0) {
        char c = static_cast<char>(Serial.read());

        if (c == '\n') {
            // End of command line
            s_rxBuf[s_rxPos] = '\0';
            if (s_rxPos > 0) {
                handleCommand(s_rxBuf);
            }
            s_rxPos = 0;
        } else if (s_rxPos < USB_RX_BUF_SIZE - 1) {
            s_rxBuf[s_rxPos++] = c;
        } else {
            // Buffer overflow — discard and reset
            LOG_WARN("USB", "RX buffer overflow — discarding packet");
            s_rxPos = 0;
        }
    }
}
