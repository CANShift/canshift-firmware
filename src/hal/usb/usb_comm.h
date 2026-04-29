#pragma once
// usb_comm.h — USB serial communication layer (Phase 1 config sync)
//
// Protocol: JSON lines over USB serial (UART0 / CP210x bridge), 115200 baud.
// Each message is one JSON object followed by \n.
//
//   Commands from desktop → device:
//     CMD_PUT_CONFIG      0x02  — Push new dashboard.json content
//     CMD_SCREEN_SETTINGS 0x05  — Push display settings (brightness, contrast, sleep, rotation)
//     CMD_REBOOT          0xF0  — Soft reboot the device
//
//   Responses from device → desktop:
//     {"status":"ok"}
//     {"status":"error","message":"..."}
//
//   Telemetry pushed by device every ~200ms (proactive, no request needed):
//     {"tele":1,"v":{"rpm":1234.5,"coolant_temp_c":89.2,...}}
//     Only valid (non-timed-out) signals are included.

#include <stdint.h>
#include <stddef.h>

namespace UsbComm {

/**
     * Initialize USB serial communication.
     * Sets up the receive buffer and state machine.
     */
void init();

/**
     * Process incoming bytes from Serial and dispatch commands.
     * Call from the USB comm task at ~20ms intervals.
     */
void tick();

// ---------------------------------------------------------------------------
// Command IDs
// ---------------------------------------------------------------------------
static constexpr uint8_t CMD_GET_CONFIG = 0x01;
static constexpr uint8_t CMD_PUT_CONFIG = 0x02;
static constexpr uint8_t CMD_PUT_SIGNALS = 0x03;
static constexpr uint8_t CMD_PUT_THEME = 0x04;
// Push screen display settings (brightness, contrast, sleep, rotation)
// Payload: {"brightness":80,"contrast":50,"sleep":0,"rotation":0}
static constexpr uint8_t CMD_SCREEN_SETTINGS = 0x05;
static constexpr uint8_t CMD_GET_STATUS = 0x10;
static constexpr uint8_t CMD_REBOOT = 0xF0;

static constexpr uint8_t RSP_OK = 0x80;
static constexpr uint8_t RSP_ERROR = 0x81;
static constexpr uint8_t RSP_DATA = 0x82;

} // namespace UsbComm
