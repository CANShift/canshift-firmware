#pragma once
// can_manager.h — TWAI (CAN) hardware manager and receive task
//
// Initializes the ESP32 TWAI controller, installs the driver,
// receives frames in a loop, and dispatches to MaxxEcuParser.
//
// The ESP32 TWAI controller supports:
//   - Standard (11-bit) and extended (29-bit) frame IDs
//   - Acceptance filter (hardware filter for frame IDs)
//   - 500 kbps / 1 Mbps / other standard CAN speeds
//
// Frame acceptance filter strategy:
//   - Accept all frames from MaxxECU known frame ID range (0x370-0x37F)
//   - Reject all other frames — reduces CPU overhead
//   - TODO: Make filter range configurable via signals.json

#include <stdint.h>

namespace CanManager {

    /**
     * Initialize TWAI hardware driver.
     * Called from BootSequence::run() (not from the CAN task).
     */
    void initHardware();

    /**
     * Main CAN receive and dispatch loop.
     * Blocks waiting for a frame (with timeout), then dispatches to parser.
     * Called repeatedly from the CAN FreeRTOS task.
     */
    void tick();

    /**
     * Return the number of CAN frames received since boot.
     */
    uint32_t getFrameCount();

    /**
     * Return the number of CAN parse errors since boot.
     */
    uint32_t getErrorCount();

} // namespace CanManager
