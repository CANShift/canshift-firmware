// theme_manager.cpp — Apply loaded theme to LVGL

#include "theme_manager.h"
#include "config/config_loader.h"
#include "diag/logger.h"
#include <lvgl.h>

void ThemeManager::apply() {
    const CfgTheme& theme = ConfigLoader::getThemeConfig();

    if (!theme.loaded) {
        LOG_WARN("THEME", "Theme not loaded — using LVGL default dark theme");
        lv_disp_t* disp = lv_disp_get_default();
        lv_theme_t* th = lv_theme_default_init(
            disp,
            lv_color_hex(0xFF4444),  // Primary: red
            lv_color_hex(0xFF8800),  // Secondary: orange
            true,                    // Dark mode
            LV_FONT_DEFAULT
        );
        lv_disp_set_theme(disp, th);
        return;
    }

    // Apply theme colors to LVGL default theme
    lv_disp_t* disp = lv_disp_get_default();
    lv_theme_t* th = lv_theme_default_init(
        disp,
        lv_color_hex(theme.primary.rgb),
        lv_color_hex(theme.accent.rgb),
        true,   // Dark mode — automotive dash is always dark
        LV_FONT_DEFAULT
    );
    lv_disp_set_theme(disp, th);

    // Apply background to default screen
    lv_obj_set_style_bg_color(lv_scr_act(),
        lv_color_hex(theme.background.rgb), LV_PART_MAIN);

    LOG_INFO("THEME", "Theme '%s' applied", theme.name);
}
