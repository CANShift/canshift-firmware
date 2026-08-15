#include "severity.h"

#include "ui/font_manager.h"
#include "ui/theme_manager.h"
#include "ui/theme_tokens.h"
#include "ui/widgets/widget_helpers.h"

#include <lvgl.h>

namespace Severity {

namespace {

using ColorFn = uint32_t (*)();

uint32_t informationInk() {
    return ThemeManager::getEffectiveTextColor();
}

uint32_t criticalInk() {
    return ThemeTokens::kInkNight;
}

constexpr ColorFn kInkFns[kLevelCount] = {informationInk, ThemeManager::warnColor, criticalInk,
                                          ThemeManager::dangerColor};

struct LevelShape {
    bool flood;
    int16_t kickerGapPx;
};

constexpr LevelShape kLevelShapes[kLevelCount] = {{false, 1}, {false, 1}, {true, 3}, {false, 3}};

uint8_t indexOf(Level level) {
    const uint8_t raw = static_cast<uint8_t>(level);
    return raw < kLevelCount ? raw : 0;
}

lv_obj_t *makeColumn(lv_obj_t *parent) {
    lv_obj_t *col = WidgetHelpers::makeFlushColumn(parent);
    WidgetHelpers::disableInteract(col);
    return col;
}

lv_obj_t *makeRule(lv_obj_t *col, uint8_t rulePx, uint32_t rgb) {
    lv_obj_t *rule = lv_obj_create(col);
    if (!rule)
        return nullptr;
    WidgetHelpers::resetContainerStyle(rule);
    lv_obj_set_size(rule, LV_PCT(100), rulePx);
    lv_obj_set_style_bg_color(rule, lv_color_hex(rgb), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, LV_PART_MAIN);
    WidgetHelpers::disableInteract(rule);
    return rule;
}

lv_obj_t *makeKicker(lv_obj_t *col, const char *text, uint32_t rgb) {
    lv_obj_t *label = lv_label_create(col);
    if (!label)
        return nullptr;
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_style_text_font(label, FontManager::label(kKickerPx), 0);
    lv_obj_set_style_text_letter_space(label, kKickerTrackingPx, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(rgb), 0);
    lv_obj_set_style_pad_top(label, kRuleGapPx, 0);
    return label;
}

lv_obj_t *makeBody(lv_obj_t *col, int16_t gapPx) {
    lv_obj_t *body = makeColumn(col);
    if (!body)
        return nullptr;
    lv_obj_set_style_pad_top(body, gapPx, LV_PART_MAIN);
    lv_obj_set_style_pad_right(body, kRightInsetPx, LV_PART_MAIN);
    return body;
}

uint32_t ruleRgbFor(const Surface &surface, Level level) {
    if (level == Level::INFORMATION)
        return surface.baseRuleRgb;
    return inkFor(level);
}

void paintRule(const Surface &surface, Level level) {
    if (!surface.rule)
        return;
    WidgetHelpers::setVisible(surface.rule, !floodsGround(level));
    lv_obj_set_style_bg_color(surface.rule, lv_color_hex(ruleRgbFor(surface, level)), LV_PART_MAIN);
}

void paintGround(const Surface &surface, Level level) {
    if (!surface.root)
        return;
    const bool flood = floodsGround(level);
    lv_obj_set_style_bg_color(surface.root, lv_color_hex(ThemeManager::dangerColor()),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(surface.root, flood ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
}

} // namespace

Level fromRaw(uint8_t raw) {
    return static_cast<Level>(indexOf(static_cast<Level>(raw)));
}

Level forReading(float value, float warnLevel, float dangerLevel, bool dangerBelow) {
    return fromRaw(alert_severity_for_reading_rs(value, warnLevel, dangerLevel, dangerBelow));
}

uint32_t inkFor(Level level) {
    return kInkFns[indexOf(level)]();
}

uint32_t kickerRgbFor(Level level) {
    if (level == Level::INFORMATION)
        return ThemeManager::dimColor();
    return inkFor(level);
}

uint32_t baseRuleRgbFor(uint8_t rulePx, uint32_t baseInkRgb) {
    if (rulePx <= kRuleSecondaryPx)
        return ThemeManager::trackColor();
    return baseInkRgb;
}

bool floodsGround(Level level) {
    return kLevelShapes[indexOf(level)].flood;
}

int16_t kickerGapFor(Level level) {
    return kLevelShapes[indexOf(level)].kickerGapPx;
}

Surface build(lv_obj_t *parent, const Spec &spec) {
    Surface surface;
    if (!parent)
        return surface;
    surface.rulePx = spec.rulePx;
    surface.baseInkRgb = ThemeManager::getEffectiveTextColor();
    surface.baseRuleRgb = baseRuleRgbFor(spec.rulePx, surface.baseInkRgb);
    surface.level = spec.level;
    surface.painted = true;
    surface.root = makeColumn(parent);
    if (!surface.root)
        return surface;
    surface.rule = makeRule(surface.root, spec.rulePx, ruleRgbFor(surface, spec.level));
    surface.kicker = makeKicker(surface.root, spec.kicker, kickerRgbFor(spec.level));
    surface.value = makeBody(surface.root, kickerGapFor(spec.level));
    paintRule(surface, spec.level);
    paintGround(surface, spec.level);
    return surface;
}

Surface adopt(lv_obj_t *rule, lv_obj_t *kicker, lv_obj_t *value, uint8_t rulePx,
              uint32_t baseInkRgb) {
    Surface surface;
    surface.rule = rule;
    surface.kicker = kicker;
    surface.value = value;
    surface.rulePx = rulePx;
    surface.baseInkRgb = baseInkRgb;
    surface.baseRuleRgb = baseRuleRgbFor(rulePx, baseInkRgb);
    return surface;
}

lv_obj_t *addReason(const Surface &surface, const char *text) {
    if (!surface.value)
        return nullptr;
    lv_obj_t *label = lv_label_create(surface.value);
    if (!label)
        return nullptr;
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_style_text_font(label, FontManager::value(kReasonPx), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(ThemeManager::getEffectiveTextColor()), 0);
    return label;
}

void setKicker(const Surface &surface, const char *text) {
    if (!surface.kicker)
        return;
    WidgetHelpers::setLabelTextIfChanged(surface.kicker, text ? text : "");
}

void repaint(Surface &surface, Level level) {
    if (surface.painted && surface.level == level)
        return;
    surface.level = level;
    surface.painted = true;
    paintRule(surface, level);
    paintGround(surface, level);
    if (surface.kicker)
        lv_obj_set_style_text_color(surface.kicker, lv_color_hex(kickerRgbFor(level)), 0);
    if (!surface.value)
        return;
    const uint32_t valueRgb = level == Level::INFORMATION ? surface.baseInkRgb : inkFor(level);
    lv_obj_set_style_text_color(surface.value, lv_color_hex(valueRgb), 0);
}

} // namespace Severity
