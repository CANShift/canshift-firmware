#pragma once
// theme_manager.h — Initialize LVGL base theme

#include <lvgl.h>

namespace ThemeManager {

/**
     * Apply the loaded theme config to the LVGL default theme.
     * Call from BootSequence::run() after ConfigLoader::loadAll().
     */
void apply();

} // namespace ThemeManager
