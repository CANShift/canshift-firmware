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

        StaticJsonDocument<256> doc;
        DeserializationError err = deserializeJson(doc, jsonLine);
        if (err) {
            LOG_WARN("USB", "JSON parse error: %s", err.c_str());
            Serial.println("{\"rsp\":129,\"msg\":\"parse_error\"}");
            return;
        }

        uint8_t cmd = doc["cmd"] | 0;
        switch (cmd) {
            case UsbComm::CMD_SCREEN_SETTINGS:
                handleScreenSettings(doc.as<JsonObjectConst>());
                break;
            default:
                LOG_DEBUG("USB", "Unhandled cmd: 0x%02X", cmd);
                Serial.println("{\"rsp\":128,\"msg\":\"ok\"}");
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
