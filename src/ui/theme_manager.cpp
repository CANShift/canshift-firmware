// theme_manager.cpp — Runtime day/night theme switching

#include "theme_manager.h"
#include "page_manager.h"
#include "config/config_loader.h"
#include "diag/logger.h"

#include <lvgl.h>
#include <Preferences.h>

// ---------------------------------------------------------------------------
// NVS
// ---------------------------------------------------------------------------

static constexpr char NVS_NS[]      = "theme";
static constexpr char KEY_DAY_MODE[] = "day_mode";

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static bool s_isDayMode = false;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ThemeManager::apply() {
    lv_disp_t *disp = lv_disp_get_default();
    lv_theme_t *th = lv_theme_default_init(
        disp,
        lv_color_hex(0xFF4444), // Primary — CANShift red
        lv_color_hex(0xFF8800), // Secondary — amber
        true,                   // Dark mode
        LV_FONT_DEFAULT);
    lv_disp_set_theme(disp, th);
    LOG_INFO("THEME", "LVGL dark theme applied");
}

void ThemeManager::init() {
    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    if (!dash.hasDayTheme) {
        s_isDayMode = false; // Day mode unavailable without config
        return;
    }

    Preferences p;
    p.begin(NVS_NS, /*readOnly=*/true);
    s_isDayMode = p.getBool(KEY_DAY_MODE, false);
    p.end();

    LOG_INFO("THEME", "Day mode: %s", s_isDayMode ? "ON" : "OFF");
}

bool ThemeManager::isDayMode() {
    return s_isDayMode;
}

void ThemeManager::toggleDayMode() {
    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    if (!dash.hasDayTheme) return;

    s_isDayMode = !s_isDayMode;

    Preferences p;
    p.begin(NVS_NS, /*readOnly=*/false);
    p.putBool(KEY_DAY_MODE, s_isDayMode);
    p.end();

    LOG_INFO("THEME", "Day mode toggled: %s", s_isDayMode ? "ON" : "OFF");

    // Rebuild all pages to apply the new palette and background colors.
    // This also updates the top bar via TopBar::reapplyTheme() called inside.
    PageManager::requestRebuild();
}

CfgColor ThemeManager::getEffectiveBgColor(const CfgColor &nightBg) {
    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    if (s_isDayMode && dash.hasDayTheme) {
        return dash.dayTheme.bgColor;
    }
    return nightBg;
}

CfgPagePalette ThemeManager::getEffectivePalette(const CfgPagePalette &nightPalette) {
    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    if (s_isDayMode && dash.hasDayTheme) {
        return dash.dayTheme.palette;
    }
    return nightPalette;
}
