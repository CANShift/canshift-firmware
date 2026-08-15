#include "system_screen.h"

#include "ui/dash_metrics.h"
#include "ui/font_manager.h"
#include "ui/theme_manager.h"
#include "ui/theme_tokens.h"
#include "ui/widgets/widget_helpers.h"

#include <lvgl.h>

namespace SystemScreen {

namespace {

constexpr uint8_t kMonoSmallPx = 10;
constexpr uint16_t kPermille = 1000;

const lv_font_t *monoFont(uint8_t devicePx) {
    if (devicePx <= kMonoSmallPx)
        return FontManager::units();
    return FontManager::value(devicePx);
}

int16_t lineSpaceFor(const lv_font_t *font, uint8_t devicePx, uint16_t lineHeightPermille) {
    const int32_t target =
        (static_cast<int32_t>(devicePx) * lineHeightPermille + kPermille / 2) / kPermille;
    return static_cast<int16_t>(target - font->line_height);
}

lv_obj_t *makeStack(lv_obj_t *parent, lv_flex_flow_t flow, int16_t gapPx) {
    lv_obj_t *stack = lv_obj_create(parent);
    if (!stack)
        return nullptr;
    WidgetHelpers::resetContainerStyle(stack);
    lv_obj_set_size(stack, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(stack, flow);
    lv_obj_set_flex_align(stack, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(stack, gapPx, LV_PART_MAIN);
    lv_obj_set_style_pad_column(stack, gapPx, LV_PART_MAIN);
    return stack;
}

lv_obj_t *makeScreen() {
    lv_obj_t *screen = lv_obj_create(nullptr);
    if (!screen)
        return nullptr;
    lv_obj_set_size(screen, LV_HOR_RES, LV_VER_RES);
    WidgetHelpers::resetContainerStyle(screen);
    lv_obj_set_style_bg_color(
        screen,
        lv_color_hex(ThemeManager::pickColor(ThemeTokens::kGroundNight, ThemeTokens::kGroundDay)),
        LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, DashMetrics::kFramePaddingPx, LV_PART_MAIN);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(screen, 0, LV_PART_MAIN);
    return screen;
}

} // namespace

Frame build(const Severity::Spec &spec, const FootSpec &foot) {
    Frame frame{};
    frame.screen = makeScreen();
    if (!frame.screen)
        return frame;
    frame.header = Severity::build(frame.screen, spec);
    if (frame.header.value) {
        lv_obj_set_style_pad_top(frame.header.value, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_right(frame.header.value, 0, LV_PART_MAIN);
    }
    frame.foot = makeStack(frame.screen, foot.flow, foot.gapPx);
    return frame;
}

lv_obj_t *addMonoLine(lv_obj_t *parent, const char *text, uint32_t rgb, uint8_t devicePx,
                      uint16_t lineHeightPermille) {
    if (!parent)
        return nullptr;
    lv_obj_t *label = lv_label_create(parent);
    if (!label)
        return nullptr;
    const lv_font_t *font = monoFont(devicePx);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(rgb), 0);
    lv_obj_set_style_text_letter_space(label, WidgetHelpers::valueTrackingPx(devicePx), 0);
    lv_obj_set_style_text_line_space(label, lineSpaceFor(font, devicePx, lineHeightPermille), 0);
    return label;
}

lv_obj_t *addKicker(lv_obj_t *parent, const char *text, uint32_t rgb) {
    if (!parent)
        return nullptr;
    lv_obj_t *label = lv_label_create(parent);
    if (!label)
        return nullptr;
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_style_text_font(label, FontManager::label(Severity::kKickerPx), 0);
    lv_obj_set_style_text_letter_space(label, Severity::kKickerTrackingPx, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(rgb), 0);
    return label;
}

void setTopGap(lv_obj_t *obj, int16_t gapPx) {
    if (!obj)
        return;
    lv_obj_set_style_pad_top(obj, gapPx, LV_PART_MAIN);
}

void present(const Frame &frame) {
    if (!frame.screen)
        return;
    lv_scr_load(frame.screen);
}

} // namespace SystemScreen
