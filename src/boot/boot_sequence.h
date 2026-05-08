#pragma once
// boot_sequence.h — Synchronous power-on initialization sequence

namespace BootSequence {

/**
     * Run the full synchronous boot sequence:
     *   1. Initialize logging
     *   2. Initialize storage (SPIFFS/SD)
     *   3. Initialize display HAL and LVGL
     *   4. Show boot splash screen
     *   5. Initialize touch HAL
     *   6. Load configuration from filesystem
     *   7. Initialize signal store
     *   8. Initialize alert engine
     *   9. Build UI from config (page manager, widgets)
     *  10. Initialize CAN HAL (unless simulation mode)
     *  11. Initialize USB comm HAL
     *  12. Dismiss splash, show main page
     *
     * If the SD card is missing or fails to mount, the boot sequence does not
     * halt: it logs a warning, marks the device as degraded (see
     * isDegradedNoSd()), and continues with built-in default config so the
     * dashboard renders and USB stays reachable from the studio.
     *
     * Call once from setup() before starting FreeRTOS tasks.
     */
void run();

/**
     * True when the boot sequence proceeded without a working SD card.
     * Built-in defaults are used and persistent settings cannot be saved.
     * Surfaced over USB (CMD_GET_STATUS "sd" field) and via a small UI badge.
     */
bool isDegradedNoSd();

} // namespace BootSequence
