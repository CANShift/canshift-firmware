#include "label_widget.h"
#include "diag/logger.h"
#include "ui/font_manager.h"
#include "ui/rev_limit_flash.h"
#include "ui/severity.h"
#include "ui/theme_manager.h"
#include "ui/widget_label.h"
#include "ui/widget_styles.h"
#include "ui/widgets/widget_helpers.h"
#include "ui/widgets/widget_tag_pool.h"
#include <cmath>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

namespace {

constexpr uint8_t kBarHeightPx = 2;
constexpr int16_t kBarSideMarginPx = 0;
constexpr int16_t kValueClusterInsetPx = 0;
constexpr int16_t kValueRowPadColumnPx = 3;
constexpr int16_t kValueRowYOffsetPx = 8;

uint8_t pickValueFontSize(const CfgWidget &cfg) {
    if (cfg.label.big > 0)
        return WidgetHelpers::deviceFontPxForBig(cfg.label.big);
    const int byHeight = (cfg.layout.h * 65) / 100;
    const int byWidth = (cfg.layout.w * 52) / 100;
    int s = byHeight < byWidth ? byHeight : byWidth;
    if (s < 10)
        s = 10;
    if (s > 48)
        s = 48;
    return static_cast<uint8_t>(s);
}

const lv_font_t *valueFontFor(uint8_t size) {
    if (size >= 17)
        return FontManager::value(size);
    return FontManager::units();
}

struct LabelTag {
    lv_obj_t *valueLabel;
    lv_obj_t *unitLabel;
    lv_obj_t *barFill;
    Severity::Surface severity;
    int16_t barMaxW;
    int16_t lastBarW;
    float warnLevel;
    float dangerLevel;
    bool dangerBelow;
    float lastValue;
    bool lastValid;
    bool revFlash;
    uint32_t baseTextRgb;
    uint32_t lastTintRgb;
    uint32_t lastBarRgb;
    char stalePlaceholder[WidgetHelpers::kStalePlaceholderCap];
};

uint32_t valueRgbFor(const LabelTag *tag, Severity::Level level) {
    if (level == Severity::Level::INFORMATION)
        return tag->baseTextRgb;
    return Severity::inkFor(level);
}

void applySeverity(LabelTag *tag, Severity::Level level) {
    Severity::repaint(tag->severity, level);
    if (!tag->barFill)
        return;
    WidgetStyles::setBgColorIfChanged(tag->barFill, tag->lastBarRgb, valueRgbFor(tag, level));
}

struct LabelParts {
    lv_obj_t *kicker = nullptr;
    lv_obj_t *valueLabel = nullptr;
    lv_obj_t *unitLabel = nullptr;
    lv_obj_t *topRule = nullptr;
    lv_obj_t *barFill = nullptr;
    int16_t barMaxW = 0;
    uint32_t textRgb = 0;
    uint8_t rulePx = Severity::kRulePrimaryPx;
};

lv_obj_t *makeValueRow(lv_obj_t *cont) {
    lv_obj_t *valueRow = lv_obj_create(cont);
    if (!valueRow)
        return nullptr;
    lv_obj_set_size(valueRow, LV_PCT(100), LV_SIZE_CONTENT);
    WidgetHelpers::resetContainerStyle(valueRow);
    lv_obj_set_style_pad_column(valueRow, kValueRowPadColumnPx, LV_PART_MAIN);
    lv_obj_set_flex_flow(valueRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(valueRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_align(valueRow, LV_ALIGN_LEFT_MID, kValueClusterInsetPx, kValueRowYOffsetPx);
    return valueRow;
}

lv_obj_t *makeValueLabel(lv_obj_t *valueRow, uint8_t valueSize) {
    lv_obj_t *label = lv_label_create(valueRow);
    if (!label)
        return nullptr;
    lv_obj_set_style_text_color(label, lv_color_hex(ThemeManager::getStaleTextColor()), 0);
    lv_obj_set_style_text_font(label, valueFontFor(valueSize), 0);
    lv_obj_set_style_text_letter_space(label, WidgetHelpers::valueTrackingPx(valueSize), 0);
    return label;
}

lv_obj_t *makeUnitLabel(lv_obj_t *valueRow, const CfgWidget &cfg) {
    const char *unit = WidgetHelpers::resolveDisplayUnit(cfg.signalId, cfg.label.suffix);
    if (unit[0] == '\0')
        return nullptr;
    lv_obj_t *unitLabel = lv_label_create(valueRow);
    if (!unitLabel)
        return nullptr;
    lv_obj_set_style_text_color(unitLabel, lv_color_hex(ThemeManager::dimColor()), 0);
    lv_obj_set_style_text_font(unitLabel, FontManager::units(), 0);
    lv_label_set_text(unitLabel, unit);
    return unitLabel;
}

lv_obj_t *makeBarRect(lv_obj_t *cont, int16_t w, uint32_t rgb) {
    lv_obj_t *rect = lv_obj_create(cont);
    if (!rect)
        return nullptr;
    lv_obj_set_size(rect, w, kBarHeightPx);
    lv_obj_align(rect, LV_ALIGN_BOTTOM_LEFT, kBarSideMarginPx, 0);
    lv_obj_set_style_bg_color(rect, lv_color_hex(rgb), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rect, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(rect, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(rect, 0, LV_PART_MAIN);
    lv_obj_clear_flag(rect, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return rect;
}

lv_obj_t *makeProgressBar(lv_obj_t *cont, const CfgWidget &cfg, uint32_t textRgb,
                          int16_t *barMaxW) {
    *barMaxW = 0;
    if (!cfg.label.showBar || cfg.label.maxValue <= cfg.label.minValue)
        return nullptr;
    const int16_t maxW = static_cast<int16_t>(cfg.layout.w - 2 * kBarSideMarginPx);
    if (maxW <= 0)
        return nullptr;
    makeBarRect(cont, maxW, ThemeManager::trackColor());
    lv_obj_t *fill = makeBarRect(cont, 1, textRgb);
    if (fill) {
        *barMaxW = maxW;
    }
    return fill;
}

bool buildLabelParts(lv_obj_t *cont, const CfgWidget &cfg, LabelParts *parts) {
    const uint8_t valueSize = pickValueFontSize(cfg);
    parts->textRgb =
        ThemeManager::getEffectiveTextColor(cfg.style.textColor.rgb, cfg.style.respectDayMode);
    parts->kicker = WidgetLabelOverlay::applySignalHeader(cont, cfg.signalId);
    lv_obj_t *valueRow = makeValueRow(cont);
    if (!valueRow) {
        LOG_ERROR("WF", "valueRow create failed for '%s' — LVGL pool OOM", cfg.id);
        return false;
    }
    parts->valueLabel = makeValueLabel(valueRow, valueSize);
    if (!parts->valueLabel) {
        LOG_ERROR("WF", "lv_label_create failed for '%s' — LVGL pool OOM", cfg.id);
        return false;
    }
    parts->unitLabel = makeUnitLabel(valueRow, cfg);
    const bool primary = valueSize >= WidgetHelpers::kRulePrimaryFontMin;
    parts->rulePx = primary ? Severity::kRulePrimaryPx : Severity::kRuleSecondaryPx;
    parts->topRule = WidgetHelpers::makeTopRule(
        cont, parts->rulePx, Severity::baseRuleRgbFor(parts->rulePx, parts->textRgb));
    parts->barFill = makeProgressBar(cont, cfg, parts->textRgb, &parts->barMaxW);
    WidgetHelpers::reportValueOverflow(
        cfg, valueFontFor(valueSize), WidgetHelpers::valueTrackingPx(valueSize),
        WidgetHelpers::resolveDisplayUnit(cfg.signalId, cfg.label.suffix));
    return true;
}

Severity::Level labelLevelFor(const LabelTag *tag, float value) {
    if (tag->revFlash && RevLimitFlash::isEngaged())
        return Severity::Level::FAILURE;
    return Severity::forReading(value, tag->warnLevel, tag->dangerLevel, tag->dangerBelow);
}

void barWidthAnimCb(void *obj, int32_t w) {
    lv_obj_set_width(static_cast<lv_obj_t *>(obj), w > 0 ? w : 1);
}

void renderStale(LabelTag *tag) {
    if (tag->lastValid) {
        WidgetHelpers::setLabelTextIfChanged(tag->valueLabel, tag->stalePlaceholder);
        tag->lastValue = NAN;
        tag->lastValid = false;
    }
    WidgetStyles::setTextColorIfChanged(tag->valueLabel, tag->lastTintRgb,
                                        ThemeManager::getStaleTextColor());
    Severity::repaint(tag->severity, Severity::Level::INFORMATION);
    if (!tag->barFill)
        return;
    if (tag->lastBarW != 0) {
        WidgetHelpers::setFillImmediate(tag->barFill, barWidthAnimCb, 1);
        tag->lastBarW = 0;
    }
    WidgetStyles::setBgColorIfChanged(tag->barFill, tag->lastBarRgb, ThemeManager::trackColor());
}

void updateBar(LabelTag *tag, const CfgWidget &cfg, float displayValue) {
    if (!tag->barFill || tag->barMaxW <= 0)
        return;
    const float pct = WidgetHelpers::clampPct(displayValue, cfg.label.minValue, cfg.label.maxValue);
    const int16_t barW = static_cast<int16_t>(pct * tag->barMaxW);
    if (barW == tag->lastBarW)
        return;
    WidgetHelpers::animateFill(tag->barFill, barWidthAnimCb,
                               static_cast<int32_t>(lv_obj_get_width(tag->barFill)), barW);
    tag->lastBarW = barW;
}

void initLabelTag(LabelTag *tag, const CfgWidget &cfg, const LabelParts &parts) {
    tag->valueLabel = parts.valueLabel;
    tag->unitLabel = parts.unitLabel;
    tag->barFill = parts.barFill;
    tag->severity =
        Severity::adopt(parts.topRule, parts.kicker, nullptr, parts.rulePx, parts.textRgb);
    tag->barMaxW = parts.barMaxW;
    tag->lastBarW = -1;
    tag->warnLevel =
        WidgetHelpers::resolveWarnLevel(cfg.signalId, cfg.label.dangerLevel, cfg.label.dangerBelow);
    tag->dangerLevel = cfg.label.dangerLevel;
    tag->dangerBelow = cfg.label.dangerBelow;
    tag->lastValue = NAN;
    tag->lastValid = false;
    tag->revFlash = cfg.label.revFlash;
    tag->baseTextRgb = parts.textRgb;
    tag->lastTintRgb = 0xFFFFFFFFu;
    tag->lastBarRgb = parts.textRgb;
    WidgetHelpers::formatStalePlaceholder(tag->stalePlaceholder, sizeof(tag->stalePlaceholder),
                                          cfg.label.maxValue);
    WidgetHelpers::setLabelTextIfChanged(tag->valueLabel, tag->stalePlaceholder);
}

} // namespace

lv_obj_t *LabelWidget::create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    lv_obj_t *cont = lv_obj_create(parent);
    if (!cont) {
        LOG_ERROR("WF", "lv_obj_create failed for '%s' — LVGL pool OOM", cfg.id);
        return nullptr;
    }
    WidgetHelpers::initContainer(cont, cfg, yOffset, cfg.style.hasBorder,
                                 cfg.style.borderColor.rgb);

    LabelParts parts;
    if (!buildLabelParts(cont, cfg, &parts)) {
        lv_obj_del(cont);
        return nullptr;
    }

    WidgetTagPool::Slot<LabelTag> tagSlot;
    LabelTag *tag = WidgetHelpers::acquireTag(tagSlot, cfg.id, "WF", cont);
    if (!tag)
        return nullptr;
    initLabelTag(tag, cfg, parts);

    lv_obj_set_user_data(cont, tag);
    lv_obj_add_event_cb(cont, WidgetTagPool::deleteHandler<LabelTag>, LV_EVENT_DELETE,
                        tagSlot.commit());

    return cont;
}

void LabelWidget::update(lv_obj_t *obj, float value, bool valid, const CfgWidget &cfg) {
    if (!obj)
        return;
    auto *tag = static_cast<LabelTag *>(lv_obj_get_user_data(obj));
    if (!tag || !tag->valueLabel)
        return;

    if (!valid) {
        renderStale(tag);
        return;
    }

    const bool unchanged = tag->lastValid && !std::isnan(tag->lastValue) && value == tag->lastValue;
    if (!unchanged) {
        char buf[40];
        WidgetHelpers::formatValue(buf, sizeof(buf), cfg.label.prefix, cfg.label.decimalPlaces,
                                   value, nullptr);
        WidgetHelpers::setLabelTextIfChanged(tag->valueLabel, buf);
        tag->lastValue = value;
        tag->lastValid = true;
    }

    const Severity::Level level = labelLevelFor(tag, value);
    WidgetStyles::setTextColorIfChanged(tag->valueLabel, tag->lastTintRgb, valueRgbFor(tag, level));

    updateBar(tag, cfg, value);
    applySeverity(tag, level);
}
