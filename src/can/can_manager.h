#pragma once
// can_manager.h — TWAI (CAN) hardware manager and receive task
//
// Initializes the ESP32 TWAI controller, installs the driver,
// receives frames in a loop, and dispatches to CanParser.
//
// The ESP32 TWAI controller supports:
//   - Standard (11-bit) and extended (29-bit) frame IDs
//   - Acceptance filter (hardware filter for frame IDs)
//   - 500 kbps / 1 Mbps / other standard CAN speeds
//
// Frame acceptance filter strategy:
//   - Accept all frames (TWAI_FILTER_CONFIG_ACCEPT_ALL)
//   - Rationale: CAN scanner mode requires all frames; signal IDs are
//     user-configurable and can span any range. At 500 kbps the ESP32
//     handles full bus traffic without meaningful overhead.

#include <stdint.h>

#include <esp_err.h>

namespace CanManager {

/**
     * Initialize TWAI hardware driver. Pinned to core 0 via a dedicated task.
     */
[[nodiscard]] esp_err_t initHardware();

/**
     * True when the TWAI driver has been successfully installed and started.
     *
     * Send/receive entry points short-circuit when this returns false so that
     * UI handlers invoked while the bus is down do not flood diagnostics
     * (issue #1229). Callers may also use this to gate UI affordances on the
     * presence of CAN hardware.
     */
[[nodiscard]] bool isAvailable();

/**
     * Main CAN receive and dispatch loop.
     * Blocks waiting for a frame (with timeout), then dispatches to parser.
     * Called repeatedly from the CAN FreeRTOS task.
     */
void tick();

/**
     * Transmit a single CAN frame.
     *
     * Non-blocking: hands the frame to the TWAI driver's TX queue and returns.
     * In simulation mode the call short-circuits to a log line and reports
     * success — there is no bus to write to, but click handlers must not
     * surface a "send failed" state for sim runs.
     *
     * @param id        11-bit (or 29-bit when extended=true) frame identifier.
     * @param data      Payload bytes; may be nullptr when len==0.
     * @param len       Payload length in bytes; values >8 are clamped to 8.
     * @param extended  When true, send as a 29-bit extended ID frame.
     *                  Currently unused by callers (reserved for follow-up).
     * @return          true on enqueue success / sim, false on TWAI error.
     */
bool sendFrame(uint32_t id, const uint8_t *data, uint8_t len, bool extended = false);

} // namespace CanManager
