#include "theme_manager.h"
#include "theme_tokens.h"
#include "page_manager.h"
#include "config/config_loader.h"
#include "diag/error_store.h"
#include "diag/logger.h"
#include "hal/storage/nvs_store.h"

#include <lvgl.h>
#include <Preferences.h>

static constexpr char NVS_NS[] = "theme";
static constexpr char KEY_DAY_MODE[] = "day_mode";

static bool s_isDayMode = false;

static void persistDayMode() {
    if (!NvsStore::putBool(NVS_NS, KEY_DAY_MODE, s_isDayMode)) {
        LOG_ERROR("THEME", "day-mode NVS write failed");
        ErrorStore::push(ERROR_SRC_SYSTEM, "nvs_write", "day mode not persisted");
    }
}

void ThemeManager::apply() {
    lv_disp_t *disp = lv_disp_get_default();
    lv_theme_t *th = lv_theme_default_init(disp, lv_color_hex(ThemeTokens::kDanger),
                                           lv_color_hex(ThemeTokens::kWarn), true, LV_FONT_DEFAULT);
    lv_disp_set_theme(disp, th);
    LOG_INFO("THEME", "LVGL dark theme applied");
}

void ThemeManager::init() {
    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    if (!dash.hasTheme) {
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
    if (!dash.hasTheme)
        return;

    s_isDayMode = !s_isDayMode;
    persistDayMode();

    LOG_INFO("THEME", "Day mode toggled: %s", s_isDayMode ? "ON" : "OFF");
    PageManager::requestRebuild();
}

void ThemeManager::setDayMode(bool day) {
    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    if (!dash.hasTheme)
        return;
    if (s_isDayMode == day)
        return;

    s_isDayMode = day;
    persistDayMode();

    LOG_INFO("THEME", "Day mode set: %s", s_isDayMode ? "ON" : "OFF");

    PageManager::requestRebuild();
}

CfgColor ThemeManager::getEffectiveBgColor(const CfgColor &nightBg) {
    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    if (!dash.hasTheme)
        return nightBg;
    return s_isDayMode ? dash.dayFace.bgColor : dash.nightFace.bgColor;
}

static const CfgTheme *activeThemeWithPalette() {
    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    if (!dash.hasTheme)
        return nullptr;
    const CfgTheme &face = s_isDayMode ? dash.dayFace : dash.nightFace;
    return face.hasPalette ? &face : nullptr;
}

uint32_t ThemeManager::getEffectiveTextColor() {
    const CfgTheme *theme = activeThemeWithPalette();
    if (theme != nullptr)
        return theme->text.rgb;
    return pickColor(ThemeTokens::kInkNight, ThemeTokens::kInkDay);
}

uint32_t ThemeManager::getEffectiveTextColor(uint32_t styleTextColor, bool respectDayMode) {
    if (!respectDayMode)
        return styleTextColor;
    return getEffectiveTextColor();
}

uint32_t ThemeManager::pickColor(uint32_t nightRgb, uint32_t dayRgb) {
    return s_isDayMode ? dayRgb : nightRgb;
}

uint32_t ThemeManager::dimColor() {
    const CfgTheme *theme = activeThemeWithPalette();
    if (theme != nullptr)
        return theme->textDim.rgb;
    return pickColor(ThemeTokens::kDimNight, ThemeTokens::kDimDay);
}

uint32_t ThemeManager::trackColor() {
    return pickColor(ThemeTokens::kTrackNight, ThemeTokens::kTrackDay);
}

uint32_t ThemeManager::warnColor() {
    return ThemeTokens::kWarn;
}

uint32_t ThemeManager::dangerColor() {
    return ThemeTokens::kDanger;
}

uint32_t ThemeManager::lockLineColor() {
    return pickColor(ThemeTokens::kLockLineNight, ThemeTokens::kLockLineDay);
}

uint32_t ThemeManager::lockInkColor() {
    return pickColor(ThemeTokens::kLockInkNight, ThemeTokens::kLockInkDay);
}

uint32_t ThemeManager::getStaleTextColor() {
    return dimColor();
}
