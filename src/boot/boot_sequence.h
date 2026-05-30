#pragma once
// boot_sequence.h — Synchronous power-on initialization sequence

namespace BootSequence {

/**
 * Run the full synchronous boot sequence:
 *   1. Initialize logging
 *   2. Initialize storage (SPIFFS)
 *   3. Initialize display HAL and LVGL
 *   4. Show boot splash screen
 *   5. Initialize touch HAL
 *   6. Load configuration from filesystem
 *   7. Initialize signal store
 *   8. Initialize alert engine
 *   9. Build UI from config (page manager, widgets)
 *  10. Initialize CAN HAL
 *  11. Initialize USB comm HAL
 *  12. Dismiss splash, show main page
 *
 * If storage fails to mount, the boot sequence does not halt: it logs an
 * error and continues with built-in default config so the dashboard renders
 * and USB stays reachable from the studio.
 *
 * Call once from setup() before starting FreeRTOS tasks.
 */
void run();

/**
 * Mark the running OTA slot as valid so the bootloader cancels its pending
 * rollback. No-op when the running partition is not in PENDING_VERIFY state
 * (factory boot, or already-marked from an earlier boot).
 *
 * Idempotent — safe to call more than once. Designed to fire from taskUI
 * after the UI loop has rendered N healthy frames (F-ME-8), so a UI-layer
 * crash during early paint still triggers rollback. Issue #674.
 */
void markOtaSlotValidIfPending();

} // namespace BootSequence
