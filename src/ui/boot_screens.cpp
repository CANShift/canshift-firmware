#include "boot_screens.h"

#include "layout_scale.h"
#include "ui/brand_mark.h"
#include "ui/font_manager.h"
#include "ui/severity.h"
#include "ui/theme_manager.h"
#include "ui/theme_tokens.h"
#include "ui/widgets/widget_helpers.h"

#include <lvgl.h>

namespace BootScreens {

namespace {

constexpr uint32_t kWordmarkRgb = 0xF3F2F2;
constexpr uint8_t kWordmarkPx = 14;
constexpr int16_t kWordmarkTrackingPx = 2;
constexpr int16_t kMarkGapPx = 13;

constexpr int16_t kFramePadPx = 8;
constexpr int16_t kHeaderGapPx = 9;
constexpr int16_t kRowPadPx = 5;
constexpr int16_t kRowRulePx = 1;

constexpr const char *kSelfTestKicker = "SELF-TEST";
constexpr const char *kFooterText = "CONTINUING IN 2 s";
constexpr const char *kCheckLabels[kCheckCount] = {"DISPLAY", "TOUCH", "FLASH", "CONFIG",
                                                   "CAN BUS"};

void paintGround(lv_obj_t *obj) {
    WidgetHelpers::resetContainerStyle(obj);
    lv_obj_set_style_bg_color(obj, lv_color_hex(ThemeTokens::kGroundNight), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
}

lv_obj_t *makeMonoLabel(lv_obj_t *parent, const char *text, uint32_t rgb) {
    lv_obj_t *label = lv_label_create(parent);
    if (!label)
        return nullptr;
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, FontManager::units(), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(rgb), 0);
    return label;
}

lv_obj_t *makeRow(lv_obj_t *parent) {
    lv_obj_t *row = lv_obj_create(parent);
    if (!row)
        return nullptr;
    WidgetHelpers::resetContainerStyle(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(row, LayoutScale::y(kRowPadPx), LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(row, LayoutScale::y(kRowPadPx), LV_PART_MAIN);
    lv_obj_set_style_border_color(row, lv_color_hex(ThemeManager::trackColor()), LV_PART_MAIN);
    lv_obj_set_style_border_width(row, kRowRulePx, LV_PART_MAIN);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    WidgetHelpers::disableInteract(row);
    return row;
}

void addRow(lv_obj_t *parent, const char *label, const CheckResult &check) {
    lv_obj_t *row = makeRow(parent);
    if (!row)
        return;
    const uint32_t warnRgb = ThemeManager::warnColor();
    const uint32_t labelRgb = check.passed ? ThemeManager::getEffectiveTextColor() : warnRgb;
    const uint32_t resultRgb = check.passed ? ThemeManager::dimColor() : warnRgb;
    makeMonoLabel(row, label, labelRgb);
    makeMonoLabel(row, check.result, resultRgb);
}

lv_obj_t *makeFrameColumn(lv_obj_t *parent) {
    lv_obj_t *column = lv_obj_create(parent);
    if (!column)
        return nullptr;
    WidgetHelpers::resetContainerStyle(column);
    lv_obj_set_size(column, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_left(column, LayoutScale::x(kFramePadPx), LV_PART_MAIN);
    lv_obj_set_style_pad_right(column, LayoutScale::x(kFramePadPx), LV_PART_MAIN);
    lv_obj_set_style_pad_top(column, LayoutScale::y(kFramePadPx), LV_PART_MAIN);
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(column, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(column, 0, LV_PART_MAIN);
    WidgetHelpers::disableInteract(column);
    return column;
}

void addFooter(lv_obj_t *parent) {
    lv_obj_t *footer = makeMonoLabel(parent, kFooterText, ThemeManager::dimColor());
    if (!footer)
        return;
    lv_obj_align(footer, LV_ALIGN_BOTTOM_LEFT, LayoutScale::x(kFramePadPx),
                 static_cast<int16_t>(-LayoutScale::y(kFramePadPx)));
}

} // namespace

void buildBoot(lv_obj_t *parent) {
    if (!parent)
        return;
    paintGround(parent);

    lv_obj_t *stack = lv_obj_create(parent);
    if (!stack)
        return;
    WidgetHelpers::resetContainerStyle(stack);
    lv_obj_set_size(stack, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(stack, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(stack, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(stack, LayoutScale::y(kMarkGapPx), LV_PART_MAIN);
    WidgetHelpers::disableInteract(stack);
    lv_obj_center(stack);

    if (!BrandMark::create(stack, true))
        return;

    lv_obj_t *wordmark = lv_label_create(stack);
    if (!wordmark)
        return;
    lv_label_set_text(wordmark, "CANSHIFT");
    lv_obj_set_style_text_font(wordmark, FontManager::label(kWordmarkPx), 0);
    lv_obj_set_style_text_letter_space(wordmark, kWordmarkTrackingPx, 0);
    lv_obj_set_style_text_color(wordmark, lv_color_hex(kWordmarkRgb), 0);
}

void buildSelfTest(lv_obj_t *parent, const CheckResult (&results)[kCheckCount]) {
    if (!parent)
        return;
    paintGround(parent);

    lv_obj_t *column = makeFrameColumn(parent);
    if (!column)
        return;

    const Severity::Spec spec = {Severity::Level::INFORMATION, kSelfTestKicker,
                                 Severity::kRulePrimaryPx};
    Severity::Surface surface = Severity::build(column, spec);
    if (!surface.value)
        return;
    lv_obj_set_style_pad_top(surface.value, LayoutScale::y(kHeaderGapPx), LV_PART_MAIN);
    lv_obj_set_style_pad_right(surface.value, 0, LV_PART_MAIN);

    for (uint8_t i = 0; i < kCheckCount; ++i)
        addRow(surface.value, kCheckLabels[i], results[i]);

    addFooter(parent);
}

} // namespace BootScreens
