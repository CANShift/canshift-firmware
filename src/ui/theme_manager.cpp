// theme_manager.cpp — Initialize LVGL base theme
//
// No configurable theme — colors are set per-page and per-widget.
// This initializes the LVGL default dark theme with CANShift brand colors
// so LVGL internals (scrollbars, focus rings, etc.) use the right palette.

#include "theme_manager.h"
#include "diag/logger.h"
#include <lvgl.h>

void ThemeManager::apply() {
    lv_disp_t *disp = lv_disp_get_default();
    lv_theme_t *th = lv_theme_default_init(disp,
                                           lv_color_hex(0xFF4444), // Primary — CANShift red
                                           lv_color_hex(0xFF8800), // Secondary — amber
                                           true,                   // Dark mode
                                           LV_FONT_DEFAULT);
    lv_disp_set_theme(disp, th);
    LOG_INFO("THEME", "LVGL dark theme applied");
}
