#include "theme_manager.h"
#include "page_manager.h"
#include "config/config_loader.h"
#include "diag/logger.h"

#include <lvgl.h>
#include <Preferences.h>

static constexpr char NVS_NS[] = "theme";
static constexpr char KEY_DAY_MODE[] = "day_mode";

static bool s_isDayMode = false;

void ThemeManager::apply() {
    lv_disp_t *disp = lv_disp_get_default();
    lv_theme_t *th = lv_theme_default_init(disp, lv_color_hex(0xFF4444), lv_color_hex(0xFF8800),
                                           true, LV_FONT_DEFAULT);
    lv_disp_set_theme(disp, th);
    LOG_INFO("THEME", "LVGL dark theme applied");
}

void ThemeManager::init() {
    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    if (!dash.hasDayTheme) {
        s_isDayMode = false;
        return;
    }

    Preferences p;
    p.begin(NVS_NS, true);
    s_isDayMode = p.getBool(KEY_DAY_MODE, false);
    p.end();

    LOG_INFO("THEME", "Day mode: %s", s_isDayMode ? "ON" : "OFF");
}

bool ThemeManager::isDayMode() {
    return s_isDayMode;
}

void ThemeManager::toggleDayMode() {
    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    if (!dash.hasDayTheme)
        return;

    s_isDayMode = !s_isDayMode;

    Preferences p;
    p.begin(NVS_NS, false);
    p.putBool(KEY_DAY_MODE, s_isDayMode);
    p.end();

    LOG_INFO("THEME", "Day mode toggled: %s", s_isDayMode ? "ON" : "OFF");
    PageManager::requestRebuild();
}

void ThemeManager::setDayMode(bool day) {
    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    if (!dash.hasDayTheme)
        return;
    if (s_isDayMode == day)
        return;

    s_isDayMode = day;

    Preferences p;
    p.begin(NVS_NS, false);
    p.putBool(KEY_DAY_MODE, s_isDayMode);
    p.end();

    LOG_INFO("THEME", "Day mode set: %s", s_isDayMode ? "ON" : "OFF");

    PageManager::requestRebuild();
}

CfgColor ThemeManager::getEffectiveBgColor(const CfgColor &nightBg) {
    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    if (s_isDayMode && dash.hasDayTheme) {
        return dash.dayTheme.bgColor;
    }
    return nightBg;
}

uint32_t ThemeManager::getEffectiveTextColor() {

    return s_isDayMode ? 0x000000u : 0xFFFFFFu;
}

uint32_t ThemeManager::getEffectiveTextColor(uint32_t styleTextColor, bool respectDayMode) {
    if (!respectDayMode)
        return styleTextColor;
    return getEffectiveTextColor();
}
