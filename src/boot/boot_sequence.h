#pragma once
// boot_sequence.h — Synchronous power-on initialization sequence

#include <cstdint>

namespace BootSequence {

/**
 * SD subsystem state observed during boot. Surfaced via getSdStatus(),
 * shown as a UI badge on the dashboard, and reported back over USB.
 */
enum class SdStatus : uint8_t {
    Ok = 0,          // SD mounted; reads/writes succeed
    NoCard = 1,      // No card detected in the slot
    MountFailed = 2, // Card detected but mount failed (pinout / speed / FS)
};

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
     * getSdStatus()), and continues with built-in default config so the
     * dashboard renders and USB stays reachable from the studio.
     *
     * Call once from setup() before starting FreeRTOS tasks.
     */
void run();

/**
 * State of the SD subsystem observed at the end of boot.
 */
SdStatus getSdStatus();

/**
 * True when the boot sequence proceeded without a working SD card.
 * Equivalent to getSdStatus() != SdStatus::Ok. Kept for backward
 * compatibility with callers that only care about the binary state.
 */
bool isDegradedNoSd();

/**
 * Attempt to mount the SD card and reload configuration after a degraded
 * boot (issue #251). Called periodically from the UI task while
 * isDegradedNoSd() is true.
 *
 * On success the internal s_sdStatus flips to Ok, the persistent SD badge
 * is cleared, default configs are provisioned if missing, ConfigLoader
 * reloads, and the page manager rebuilds the dashboard from the new
 * config. On failure the status is updated (NoCard or MountFailed) and
 * the badge is refreshed accordingly so the user sees the latest state.
 *
 * Returns true when the SD ended this call mounted and ready.
 *
 * MUST be called from the UI task while holding g_lvglMutex.
 */
bool tryRecoverSd();

} // namespace BootSequence
