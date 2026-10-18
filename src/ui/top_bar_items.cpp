#include "top_bar_items.h"
#include "top_bar_internal.h"

#include "icon_assets.h"
#include "layout_scale.h"
#include "ui/font_manager.h"
#include "theme_manager.h"
#include "ui/widgets/widget_helpers.h"

#include <lvgl.h>
#include <math.h>

using namespace TopBarInternal;

namespace {

namespace TopBarMetrics {
constexpr float kDotRatio = 0.30f;
constexpr float kGapRatio = 0.25f;
constexpr float kPaddingRatio = 0.40f;
}

int16_t metricRound(float v) {
    return static_cast<int16_t>(lroundf(v));
}
int16_t derivedDot(int16_t height) {
    return metricRound(static_cast<float>(height) * TopBarMetrics::kDotRatio);
}

lv_obj_t *makeStatusDot(lv_obj_t *parent) {
    const int16_t side = derivedDot(s_height);
    return WidgetHelpers::makeSquareBadge(parent, side, kColorDotDown);
}

lv_obj_t *makeFlagBadge(lv_obj_t *parent, const char *text) {
    lv_obj_t *cont = lv_obj_create(parent);
    WidgetHelpers::resetContainerStyle(cont);
    lv_obj_set_size(cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(cont, LayoutScale::x(kFlagGapPx), LV_PART_MAIN);
    WidgetHelpers::makeSquareBadge(cont, kFlagSquarePx, kColorModeActive);

    lv_obj_t *lbl = lv_label_create(cont);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, FontManager::units(), 0);
    lv_obj_set_style_text_color(cont, lv_color_hex(kColorModeActive), 0);
    lv_obj_update_layout(cont);
    return cont;
}

lv_obj_t *makeBarLabel(lv_obj_t *parent, const char *text, uint32_t color) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);

    lv_obj_set_style_text_font(lbl, FontManager::units(), 0);
    lv_obj_set_style_text_letter_space(lbl, kBarLetterSpacePx, 0);
    return lbl;
}

lv_obj_t *makeBarSeparator(lv_obj_t *parent, uint32_t color) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, "|");
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(lbl, FontManager::units(), 0);
    return lbl;
}

lv_obj_t *hidden(lv_obj_t *obj) {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    return obj;
}

lv_obj_t *buildStatusDot(const CfgTopBarItem &, bool) {
    return makeStatusDot(s_bar);
}

lv_obj_t *buildLabel(const CfgTopBarItem &item, bool) {
    return makeBarLabel(s_bar, item.text, mutedColor());
}

lv_obj_t *buildSeparator(const CfgTopBarItem &, bool) {
    return hidden(makeBarSeparator(s_bar, mutedColor()));
}

lv_obj_t *buildValueLabel(const CfgTopBarItem &, bool) {
    return hidden(makeBarLabel(s_bar, "", labelColor()));
}

lv_obj_t *buildBleIcon(const CfgTopBarItem &, bool) {
    lv_obj_t *obj = lv_label_create(s_bar);
    lv_label_set_text(obj, "BLE");
    lv_obj_set_style_text_color(obj, lv_color_hex(ThemeManager::getStaleTextColor()), 0);
    lv_obj_set_style_text_font(obj, FontManager::units(), 0);
    return obj;
}

lv_obj_t *buildModeFlag(const CfgTopBarItem &item, bool) {
    return hidden(makeFlagBadge(s_bar, item.text));
}

lv_obj_t *buildCanRate(const CfgTopBarItem &, bool) {
    return makeBarLabel(s_bar, kBusRateSilent, labelColor());
}

void onThemeToggleClicked(lv_event_t *) {
    ThemeManager::toggleDayMode();
}

lv_obj_t *buildThemeToggle(const CfgTopBarItem &, bool hasTheme) {
    lv_obj_t *obj = lv_img_create(s_bar);
    lv_img_set_src(
        obj, IconAssets::resolveSource(ThemeManager::isDayMode() ? "icon_day" : "icon_night"));
    s_themeIcon = obj;
    if (!hasTheme)
        return hidden(obj);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(obj, 10);
    lv_obj_add_event_cb(obj, onThemeToggleClicked, LV_EVENT_CLICKED, nullptr);
    return obj;
}

constexpr TopBarItems::KindOps kItemKinds[] = {
    {TopBarItemKind::STATUS_DOT, buildStatusDot, true, true},
    {TopBarItemKind::LABEL, buildLabel, true, true},
    {TopBarItemKind::SEPARATOR, buildSeparator, true, false},
    {TopBarItemKind::SIGNAL, buildValueLabel, true, true},
    {TopBarItemKind::SIGNAL_MAX, buildValueLabel, true, true},
    {TopBarItemKind::BLE_ICON, buildBleIcon, true, true},
    {TopBarItemKind::MODE_FLAG, buildModeFlag, true, true},
    {TopBarItemKind::TRACK_BADGE, buildValueLabel, true, true},
    {TopBarItemKind::CAN_RATE, buildCanRate, true, true},
    {TopBarItemKind::THEME_TOGGLE, buildThemeToggle, false, false},
};

}

namespace TopBarItems {

int16_t derivedGap(int16_t height) {
    return metricRound(static_cast<float>(height) * TopBarMetrics::kGapRatio);
}
int16_t derivedPadding(int16_t height) {
    return metricRound(static_cast<float>(height) * TopBarMetrics::kPaddingRatio);
}

const KindOps *opsFor(TopBarItemKind kind) {
    for (const KindOps &ops : kItemKinds) {
        if (ops.kind == kind)
            return &ops;
    }
    return nullptr;
}

}
