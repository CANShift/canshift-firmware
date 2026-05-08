#pragma once
// usb_comm.h — USB serial communication layer (Phase 1 config sync)
//
// Protocol: JSON lines over USB serial (UART0 / CP210x bridge), 115200 baud.
// Each message is one JSON object followed by \n.
//
//   Commands from desktop → device:
//     CMD_PUT_CONFIG        0x02  — Push new dashboard.json content
//     CMD_SCREEN_SETTINGS   0x05  — Push display settings (brightness, sleep)
//     CMD_PUT_FILE          0x06  — Stream a file to SD in base64-encoded chunks
//     CMD_TOGGLE_DAY_NIGHT  0x07  — Flip the day/night theme on the device
//     CMD_CALIBRATE_TOUCH   0x08  — Run the on-device touch calibration crosshairs
//     CMD_GET_STATUS        0x10  — Query firmware version, protocol, is_day flag
//     CMD_CAN_SCAN_START    0x20  — Start forwarding raw CAN frames over USB
//     CMD_CAN_SCAN_STOP     0x21  — Stop forwarding raw CAN frames
//     CMD_REBOOT            0xF0  — Soft reboot the device
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
// Push screen display settings (brightness, sleep)
// Payload: {"brightness":80,"sleep":0}
static constexpr uint8_t CMD_SCREEN_SETTINGS = 0x05;
// Stream a file to SD in chunks. One JSON line per chunk:
//   {"cmd":6,"path":"/assets/x.bin","total":N,"idx":i,"data":"<base64>"}
// idx=0 truncates / creates the target file. idx=total-1 closes it.
// Each chunk is ack'd; out-of-sequence chunks abort the transfer.
static constexpr uint8_t CMD_PUT_FILE = 0x06;
// Flip the day/night theme on the device. Deferred to the UI task because
// ThemeManager::toggleDayMode() rebuilds LVGL pages and must hold g_lvglMutex.
// Payload: {"cmd":7}
static constexpr uint8_t CMD_TOGGLE_DAY_NIGHT = 0x07;
// Run the on-device touch calibration crosshairs. Deferred to the UI task —
// calibrate() blocks while the user taps the four corners and draws via TFT_eSPI
// directly (not LVGL), so it must NOT hold g_lvglMutex.
// Payload: {"cmd":8}
static constexpr uint8_t CMD_CALIBRATE_TOUCH = 0x08;
// Set the day/night theme explicitly (idempotent). Deferred to the UI task.
// Payload: {"cmd":9,"day":true|false}
// Preferred over CMD_TOGGLE_DAY_NIGHT because tapping "Day" while already in
// day mode no longer flips the theme (issue #225).
static constexpr uint8_t CMD_SET_DAY_NIGHT = 0x09;
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

/**
 * Take-and-clear the pending day/night-toggle flag set by CMD_TOGGLE_DAY_NIGHT.
 * Returns true exactly once per command. Consumed by the UI task in main.cpp
 * while holding g_lvglMutex.
 */
bool takePendingDayNightToggle();

/**
 * Take-and-clear the pending explicit day/night set request set by CMD_SET_DAY_NIGHT.
 * Returns 1 (day), 0 (night) or -1 (no pending request). Consumed by the UI task
 * in main.cpp while holding g_lvglMutex. Prefer this over the toggle path when
 * both are pending — explicit intent wins.
 */
int8_t takePendingDayNightSet();

/**
 * Take-and-clear the pending touch-calibration flag set by CMD_CALIBRATE_TOUCH.
 * Returns true exactly once per command. Consumed by the UI task in main.cpp
 * WITHOUT holding g_lvglMutex (calibrate() draws via TFT_eSPI directly and
 * blocks on user input).
 */
bool takePendingCalibration();

/**
 * Write a single wire-protocol line to UART0 under the logger mutex.
 * `line` must be a complete JSON object terminated with '\n'.
 * Used internally by every ack / telemetry / can / can_stat write so logger
 * emits from other tasks can never fragment the line.
 */
void sendLine(const char *line);

} // namespace UsbComm
