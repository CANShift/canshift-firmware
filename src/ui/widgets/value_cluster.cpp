#include "ui/widgets/value_cluster.h"

#include "ui/font_manager.h"
#include "ui/severity.h"
#include "ui/theme_manager.h"
#include "ui/widgets/widget_helpers.h"

namespace ValueCluster {

namespace {

constexpr int16_t kClusterInsetPx = 0;
constexpr int16_t kRowPadColumnPx = 3;
constexpr int16_t kRowYOffsetPx = 8;

}

lv_obj_t *makeRow(lv_obj_t *cont) {
    lv_obj_t *row = lv_obj_create(cont);
    if (!row)
        return nullptr;
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    WidgetHelpers::resetContainerStyle(row);
    lv_obj_set_style_pad_column(row, kRowPadColumnPx, LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_align(row, LV_ALIGN_LEFT_MID, kClusterInsetPx, kRowYOffsetPx);
    return row;
}

lv_obj_t *makeValue(lv_obj_t *row, uint8_t devicePx) {
    if (!row)
        return nullptr;
    lv_obj_t *label = lv_label_create(row);
    if (!label)
        return nullptr;
    lv_obj_set_style_text_color(label, lv_color_hex(ThemeManager::getStaleTextColor()), 0);
    lv_obj_set_style_text_font(label, FontManager::value(devicePx), 0);
    lv_obj_set_style_text_letter_space(label, WidgetHelpers::valueTrackingPx(devicePx), 0);
    return label;
}

lv_obj_t *makeUnit(lv_obj_t *row, const char *unit) {
    if (!row || !unit || unit[0] == '\0')
        return nullptr;
    lv_obj_t *label = lv_label_create(row);
    if (!label)
        return nullptr;
    lv_obj_set_style_text_color(label, lv_color_hex(ThemeManager::dimColor()), 0);
    lv_obj_set_style_text_font(label, FontManager::units(), 0);
    lv_label_set_text(label, unit);
    return label;
}

uint8_t rulePxFor(uint8_t devicePx) {
    if (devicePx >= WidgetHelpers::kRulePrimaryFontMin)
        return Severity::kRulePrimaryPx;
    return Severity::kRuleSecondaryPx;
}

lv_obj_t *makeRule(lv_obj_t *cont, uint8_t devicePx, uint32_t inkRgb) {
    const uint8_t rulePx = rulePxFor(devicePx);
    return WidgetHelpers::makeTopRule(cont, rulePx, Severity::baseRuleRgbFor(rulePx, inkRgb));
}

}
