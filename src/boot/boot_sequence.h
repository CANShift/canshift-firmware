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
     * Call once from setup() before starting FreeRTOS tasks.
     * Panics (infinite loop) on any unrecoverable error.
     */
void run();

} // namespace BootSequence
