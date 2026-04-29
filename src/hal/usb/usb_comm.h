#pragma once
// usb_comm.h — USB serial communication layer (Phase 1 config sync)
//
// Phase 1 strategy: configuration is exchanged over USB serial (UART0).
// The desktop config studio (config-studio-desktop) connects to the ESP32
// over USB serial and sends JSON-framed commands.
//
// Protocol (v1):
//   Each packet is framed as:
//     [START_BYTE][2-byte length, big-endian][payload bytes][END_BYTE][CRC8]
//
//   Commands from desktop → device:
//     CMD_GET_CONFIG   0x01  — Request current config JSON
//     CMD_PUT_CONFIG   0x02  — Push new dashboard.json content
//     CMD_PUT_SIGNALS  0x03  — Push new signals.json content
//     CMD_PUT_THEME    0x04  — Push new theme.json content
//     CMD_GET_STATUS   0x10  — Request firmware status (version, uptime, signals)
//     CMD_REBOOT       0xF0  — Soft reboot the device
//
//   Responses from device → desktop:
//     RSP_OK           0x80
//     RSP_ERROR        0x81
//     RSP_DATA         0x82  — Followed by payload
//
// TODO: Finalize protocol with desktop implementation.
//       Consider switching to length-prefixed JSON (simpler) if binary framing
//       adds complexity without benefit.

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
    static constexpr uint8_t CMD_GET_CONFIG      = 0x01;
    static constexpr uint8_t CMD_PUT_CONFIG      = 0x02;
    static constexpr uint8_t CMD_PUT_SIGNALS     = 0x03;
    static constexpr uint8_t CMD_PUT_THEME       = 0x04;
    // Push screen display settings (brightness, contrast, sleep, rotation)
    // Payload: {"brightness":80,"contrast":50,"sleep":0,"rotation":0}
    static constexpr uint8_t CMD_SCREEN_SETTINGS = 0x05;
    static constexpr uint8_t CMD_GET_STATUS      = 0x10;
    static constexpr uint8_t CMD_REBOOT          = 0xF0;

    static constexpr uint8_t RSP_OK    = 0x80;
    static constexpr uint8_t RSP_ERROR = 0x81;
    static constexpr uint8_t RSP_DATA  = 0x82;

} // namespace UsbComm
