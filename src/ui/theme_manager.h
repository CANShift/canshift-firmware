#pragma once
// theme_manager.h — Runtime day/night theme switching
//
// Manages the active display theme (day vs night).
// The active mode is persisted in NVS and survives power cycles.
//
// Day mode is only available when config.hasDayTheme == true.
// When active, pages use dayTheme.bgColor + dayTheme.palette instead of
// their per-page night colors.
//
// All public functions must be called from the UI task (holds g_lvglMutex).

#include <lvgl.h>
#include "config/config_types.h"

namespace ThemeManager {

/**
 * Apply the LVGL base dark theme (CANShift brand colors).
 * Call from BootSequence::run() after LVGL is ready.
 */
void apply();

/**
 * Load persisted day/night preference from NVS.
 * Call from PageManager::init() after the config is loaded.
 */
void init();

/** True when day mode is currently active. */
bool isDayMode();

/**
 * Toggle day <-> night mode.
 * Persists to NVS, then calls PageManager::requestRebuild().
 */
void toggleDayMode();

/**
 * Return effective page background: dayTheme.bgColor when in day mode
 * and hasDayTheme, otherwise returns nightBg unchanged.
 */
CfgColor getEffectiveBgColor(const CfgColor &nightBg);

/**
 * Return effective palette: dayTheme.palette when in day mode
 * and hasDayTheme, otherwise returns nightPalette unchanged.
 */
CfgPagePalette getEffectivePalette(const CfgPagePalette &nightPalette);

/**
 * Return the effective widget text color for the current mode.
 *  - Day mode → black (0x000000) — readable on the light day backgrounds.
 *  - Night mode → white (0xFFFFFF) — readable on the dark night backgrounds.
 *
 * This deliberately overrides per-widget bespoke text colours (cyan COOLANT,
 * orange OIL, …) so all dashboard typography stays legible against whichever
 * background the active mode renders. The top bar reads its own colour
 * constants so it is unaffected.
 */
uint32_t getEffectiveTextColor();

} // namespace ThemeManager
