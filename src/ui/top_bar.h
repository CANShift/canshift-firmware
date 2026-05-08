#pragma once
// top_bar.h — Persistent top status bar
//
// The top bar is a fixed overlay (lv_layer_top()) that persists across page changes.
// It shows:
//   - Map name / profile from the ECU (SignalIds::MAP_NUMBER, MAP_NAME_IDX)
//   - Optional status icons (MIL, launch control, etc.)
//   - Optional page indicator dots
//   - A reserved "warnings" cluster on the top-right for firmware-driven
//     runtime warnings (NO SD, future: CAN errors, low fuel, etc.)
//
// Height is configured in CfgTopBar (default 24px).

#include <cstdint>
#include <lvgl.h>

namespace TopBar {

/**
 * Firmware-driven runtime warnings rendered in the top-right warnings
 * cluster. Distinct from user-configured topbar items: the user cannot
 * remove these — they are reserved for safety / boot diagnostics.
 *
 * Add new kinds at the end and bump WarningKindCount accordingly. Each
 * kind has a fixed badge text + color resolved internally.
 */
enum class WarningKind : uint8_t {
    NoSd = 0,    // SD card missing — running with built-in defaults
    SdError = 1, // SD detected but mount/read failed — check wiring/format
};
constexpr uint8_t WarningKindCount = 2;

/**
 * Create the top bar on lv_layer_top().
 * Call once from PageManager::init().
 */
void init();

/**
 * Update the top bar content (map name, icons).
 * Call from the UI task at a reduced rate (e.g. every 500ms).
 */
void update();

/**
 * Return the top bar height in pixels.
 * Used by page manager to offset widget y positions.
 */
int16_t getHeight();

/**
 * Re-apply bar background and text colors after a theme switch.
 * Called by PageManager::rebuildAllPages() after rebuilding page screens.
 * Also updates the theme toggle button icon to reflect the new mode.
 */
void reapplyTheme();

/**
 * Toggle a firmware-driven runtime warning in the top-right warnings cluster.
 *
 * Idempotent — calling with the same `active` value twice is a no-op.
 * Re-renders the cluster so badges remain right-aligned and packed.
 *
 * MUST be called while holding g_lvglMutex (touches LVGL state).
 */
void setWarning(WarningKind kind, bool active);

} // namespace TopBar
