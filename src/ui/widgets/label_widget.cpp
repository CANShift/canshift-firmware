#include "label_widget.h"
#include "diag/logger.h"
#include "ui/alert_flash.h"
#include "ui/font_manager.h"
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

constexpr const char *kStalePlaceholder = "- -";

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
    lv_obj_t *topRule;
    lv_obj_t *kicker;
    lv_obj_t *barFill;
    int16_t barMaxW;
    int16_t lastBarW;
    float alertThreshold;
    AlertFlash::State alert;
    float lastValue;
    bool lastValid;
    bool lastDangerActive;
    uint32_t baseTextRgb;
    uint32_t lastTintRgb;
    uint32_t ruleBaseRgb;
    uint32_t ruleLastRgb;
    uint32_t kickerLastRgb;
};

void setRuleColorIfChanged(LabelTag *tag, uint32_t rgb) {
    WidgetHelpers::setRuleColorIfChanged(tag->topRule, tag->ruleLastRgb, rgb);
}

void applyDangerState(LabelTag *tag);

void applyDangerAppearance(LabelTag *tag, bool danger) {
    if (tag->barFill) {
        lv_obj_set_style_bg_color(
            tag->barFill, lv_color_hex(danger ? WidgetHelpers::kZoneDangerRgb : tag->baseTextRgb),
            LV_PART_MAIN);
    }
    if (tag->kicker) {
        const uint32_t kickerRgb =
            danger ? WidgetHelpers::kZoneDangerRgb : ThemeManager::dimColor();
        if (tag->kickerLastRgb != kickerRgb) {
            lv_obj_set_style_text_color(tag->kicker, lv_color_hex(kickerRgb), 0);
            tag->kickerLastRgb = kickerRgb;
        }
    }
}

void applyDangerState(LabelTag *tag) {
    setRuleColorIfChanged(tag,
                          tag->alert.active ? WidgetHelpers::kZoneDangerRgb : tag->ruleBaseRgb);
    if (tag->alert.active == tag->lastDangerActive)
        return;
    tag->lastDangerActive = tag->alert.active;
    applyDangerAppearance(tag, tag->alert.active);
}

struct LabelParts {
    lv_obj_t *kicker = nullptr;
    lv_obj_t *valueLabel = nullptr;
    lv_obj_t *unitLabel = nullptr;
    lv_obj_t *topRule = nullptr;
    lv_obj_t *barFill = nullptr;
    int16_t barMaxW = 0;
    uint32_t textRgb = 0;
    uint32_t ruleRgb = 0;
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
    lv_label_set_text(label, kStalePlaceholder);
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
    parts->ruleRgb = primary ? parts->textRgb : ThemeManager::trackColor();
    parts->topRule = WidgetHelpers::makeTopRule(
        cont, primary ? WidgetHelpers::kRulePrimaryPx : WidgetHelpers::kRuleSecondaryPx,
        parts->ruleRgb);
    parts->barFill = makeProgressBar(cont, cfg, parts->textRgb, &parts->barMaxW);
    return true;
}

void renderStale(LabelTag *tag) {
    if (tag->lastValid) {
        WidgetHelpers::setLabelTextIfChanged(tag->valueLabel, kStalePlaceholder);
        tag->lastValue = NAN;
        tag->lastValid = false;
    }
    if (!tag->alert.active) {
        WidgetStyles::setTextColorIfChanged(tag->valueLabel, tag->lastTintRgb,
                                            ThemeManager::getStaleTextColor());
    }
    if (tag->barFill && tag->lastBarW != 0) {
        lv_obj_set_width(tag->barFill, 1);
        tag->lastBarW = 0;
    }
    AlertFlash::update(tag->alert, 0.0f, tag->alertThreshold);
    applyDangerState(tag);
}

void updateBar(LabelTag *tag, const CfgWidget &cfg, float displayValue) {
    if (!tag->barFill || tag->barMaxW <= 0)
        return;
    const float range = cfg.label.maxValue - cfg.label.minValue;
    float pct = range > 0.0f ? (displayValue - cfg.label.minValue) / range : 0.0f;
    if (pct < 0.0f)
        pct = 0.0f;
    if (pct > 1.0f)
        pct = 1.0f;
    const int16_t barW = static_cast<int16_t>(pct * tag->barMaxW);
    if (barW != tag->lastBarW) {
        lv_obj_set_width(tag->barFill, barW > 0 ? barW : 1);
        tag->lastBarW = barW;
    }
}

void initLabelTag(LabelTag *tag, const CfgWidget &cfg, const LabelParts &parts) {
    tag->valueLabel = parts.valueLabel;
    tag->unitLabel = parts.unitLabel;
    tag->topRule = parts.topRule;
    tag->kicker = parts.kicker;
    tag->barFill = parts.barFill;
    tag->barMaxW = parts.barMaxW;
    tag->lastBarW = -1;
    tag->alertThreshold = cfg.label.alertThreshold;
    tag->lastValue = NAN;
    tag->lastValid = false;
    tag->baseTextRgb = parts.textRgb;
    tag->lastTintRgb = 0xFFFFFFFFu;
    tag->ruleBaseRgb = parts.ruleRgb;
    tag->ruleLastRgb = parts.ruleRgb;
    tag->kickerLastRgb = ThemeManager::dimColor();
    tag->lastDangerActive = false;
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

    AlertFlash::attach(tag->alert, cont);
    AlertFlash::watchLabel(tag->alert, tag->valueLabel, parts.textRgb);

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

    if (!tag->alert.active) {
        WidgetStyles::setTextColorIfChanged(tag->valueLabel, tag->lastTintRgb, tag->baseTextRgb);
    }

    updateBar(tag, cfg, value);

    AlertFlash::update(tag->alert, value, tag->alertThreshold);
    applyDangerState(tag);
}
