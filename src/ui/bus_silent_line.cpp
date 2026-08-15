#include "bus_silent_line.h"

#include "dash_metrics.h"
#include "runtime/bus_health.h"
#include "ui/font_manager.h"
#include "ui/theme_manager.h"
#include "ui/theme_tokens.h"
#include "ui/widgets/widget_helpers.h"

#include <lvgl.h>
#include <stdint.h>
#include <stdio.h>

namespace {

constexpr const char *kTextFormat = "BUS SILENT %lu s - CHECK WIRING";
constexpr size_t kTextCap = 40;
constexpr uint32_t kSecondsCap = 999;
constexpr uint32_t kSecondsUnset = UINT32_MAX;

lv_obj_t *s_line = nullptr;
uint32_t s_lastSeconds = kSecondsUnset;

uint32_t groundRgb() {
    const CfgColor ground = {
        ThemeManager::pickColor(ThemeTokens::kGroundNight, ThemeTokens::kGroundDay)};
    return ThemeManager::getEffectiveBgColor(ground).rgb;
}

void applyColors(lv_obj_t *line) {
    lv_obj_set_style_text_color(line, lv_color_hex(ThemeManager::warnColor()), 0);
    lv_obj_set_style_bg_color(line, lv_color_hex(groundRgb()), LV_PART_MAIN);
}

lv_obj_t *createLine() {
    lv_obj_t *line = lv_label_create(lv_layer_top());
    if (!line)
        return nullptr;
    lv_label_set_text(line, "");
    lv_obj_set_width(line, LV_HOR_RES);
    lv_obj_set_style_text_font(line, FontManager::units(), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(line, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(line, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(line, DashMetrics::kFramePaddingPx, LV_PART_MAIN);
    lv_obj_set_style_pad_top(line, DashMetrics::kRowGapPx, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(line, DashMetrics::kFramePaddingPx, LV_PART_MAIN);
    applyColors(line);
    lv_obj_align(line, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_add_flag(line, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_background(line);
    return line;
}

} // namespace

void BusSilentLine::init() {
    s_line = createLine();
    s_lastSeconds = kSecondsUnset;
}

void BusSilentLine::reapplyTheme() {
    if (!s_line)
        return;
    applyColors(s_line);
}

void BusSilentLine::update() {
    if (!s_line)
        return;

    const BusHealth::State bus = BusHealth::sample();
    WidgetHelpers::setVisibleIfChanged(s_line, bus.silent);
    if (!bus.silent) {
        s_lastSeconds = kSecondsUnset;
        return;
    }

    const uint32_t seconds = bus.seconds > kSecondsCap ? kSecondsCap : bus.seconds;
    if (seconds == s_lastSeconds)
        return;
    s_lastSeconds = seconds;

    char text[kTextCap];
    snprintf(text, sizeof(text), kTextFormat, static_cast<unsigned long>(seconds));
    lv_label_set_text(s_line, text);
}
