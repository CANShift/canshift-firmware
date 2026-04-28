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

#include <Arduino.h>

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

    void handleCommand(const char* jsonLine) {
        // TODO: Parse jsonLine with ArduinoJson
        // For now, just log receipt
        LOG_DEBUG("USB", "Received command: %.40s...", jsonLine);

        // Stub response
        Serial.println("{\"rsp\":128,\"msg\":\"ok\"}");
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
