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

} // namespace ThemeManager
