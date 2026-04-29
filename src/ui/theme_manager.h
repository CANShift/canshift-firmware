#pragma once
// theme_manager.h — Apply loaded theme to LVGL

#include <lvgl.h>

namespace ThemeManager {

/**
     * Apply the loaded theme config to the LVGL default theme.
     * Call from BootSequence::run() after ConfigLoader::loadAll().
     */
void apply();

} // namespace ThemeManager
