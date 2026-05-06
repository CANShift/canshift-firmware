#pragma once
// usb_comm.h — USB serial communication layer (Phase 1 config sync)
//
// Protocol: JSON lines over USB serial (UART0 / CP210x bridge), 115200 baud.
// Each message is one JSON object followed by \n.
//
//   Commands from desktop → device:
//     CMD_PUT_CONFIG      0x02  — Push new dashboard.json content
//     CMD_SCREEN_SETTINGS 0x05  — Push display settings (brightness, sleep, rotation)
//     CMD_GET_STATUS      0x10  — Query firmware version and protocol number
//     CMD_CAN_SCAN_START  0x20  — Start forwarding raw CAN frames over USB
//     CMD_CAN_SCAN_STOP   0x21  — Stop forwarding raw CAN frames
//     CMD_REBOOT          0xF0  — Soft reboot the device
//
//   Responses from device → desktop:
//     {"status":"ok"}
//     {"status":"error","message":"..."}
//
//   Telemetry pushed by device every ~200ms (proactive, no request needed):
//     {"tele":1,"v":{"rpm":1234.5,"coolant_temp_c":89.2,...}}
//     Only valid (non-timed-out) signals are included.
//
//   CAN scan frames (pushed when scan mode active, one per received frame):
//     {"can":1,"id":291,"len":8,"d":[0,1,2,3,4,5,6,7]}
//     id is the raw CAN frame identifier (decimal).
//     Frames are queued from the CAN task and drained in tick().

#include <stdint.h>
#include <stddef.h>

namespace UsbComm {

/**
     * Initialize USB serial communication.
     * Sets up the receive buffer, state machine, and CAN scan queue.
     */
void init();

/**
     * Process incoming bytes from Serial and dispatch commands.
     * Also drains the CAN scan queue and sends queued frames.
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
// Push screen display settings (brightness, sleep, rotation)
// Payload: {"brightness":80,"sleep":0,"rotation":0}
static constexpr uint8_t CMD_SCREEN_SETTINGS = 0x05;
static constexpr uint8_t CMD_GET_STATUS = 0x10;
static constexpr uint8_t CMD_CAN_SCAN_START = 0x20;
static constexpr uint8_t CMD_CAN_SCAN_STOP = 0x21;
static constexpr uint8_t CMD_REBOOT = 0xF0;

static constexpr uint8_t RSP_OK = 0x80;
static constexpr uint8_t RSP_ERROR = 0x81;
static constexpr uint8_t RSP_DATA = 0x82;

// ---------------------------------------------------------------------------
// CAN scan frame — pushed from CAN task into the USB send queue
// ---------------------------------------------------------------------------

struct CanScanFrame {
    uint32_t id;     ///< Raw CAN frame identifier (11-bit or 29-bit)
    uint8_t  len;    ///< Data length code (0-8)
    uint8_t  data[8];
};

/**
 * Enqueue a raw CAN frame for USB forwarding.
 * Called from the CAN task (core 0). Thread-safe via FreeRTOS queue.
 * Returns false if scan mode is inactive or the queue is full (frame is dropped).
 */
bool pushCanFrame(const CanScanFrame &frame);

/**
 * Update the CAN health stats to be emitted on the next tick().
 * Called from the CAN task (core 0). Written with volatile — safe for our use case
 * (worst case: one stale read on the USB task side, acceptable for a status display).
 *
 * fpsX10: frames per second multiplied by 10 (e.g. 125 → 12.5 fps)
 * errors: total TWAI receive errors since boot
 */
void updateCanStats(uint32_t fpsX10, uint32_t errors);

/**
 * Returns true if the desktop host has sent any command in the last few seconds.
 * Used by the top bar to show a "host connected" icon.
 */
bool isHostActive();

} // namespace UsbComm
