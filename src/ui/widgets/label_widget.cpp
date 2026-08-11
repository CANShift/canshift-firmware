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
            danger ? WidgetHelpers::kZoneDangerRgb : WidgetLabelOverlay::kLabelDimRgb;
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

} // namespace

lv_obj_t *LabelWidget::create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    lv_obj_t *cont = lv_obj_create(parent);
    if (!cont) {
        LOG_ERROR("WF", "lv_obj_create failed for '%s' — LVGL pool OOM", cfg.id);
        return nullptr;
    }
    WidgetHelpers::initContainer(cont, cfg, yOffset, cfg.style.hasBorder,
                                 cfg.style.borderColor.rgb);

    const int16_t valueLineH = cfg.layout.h;

    const uint8_t valueSize = pickValueFontSize(cfg);
    const lv_font_t *valueFont = valueFontFor(valueSize);

    lv_obj_t *kicker = WidgetLabelOverlay::applySignalHeader(cont, cfg.signalId);

    lv_obj_t *valueRow = lv_obj_create(cont);
    if (!valueRow) {
        LOG_ERROR("WF", "valueRow create failed for '%s' — LVGL pool OOM", cfg.id);
        lv_obj_del(cont);
        return nullptr;
    }
    lv_obj_set_size(valueRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(valueRow, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(valueRow, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(valueRow, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(valueRow, 3, LV_PART_MAIN);
    lv_obj_clear_flag(valueRow, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(valueRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(valueRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_align(valueRow, LV_ALIGN_LEFT_MID, kValueClusterInsetPx, 8);

    lv_obj_t *label = lv_label_create(valueRow);
    if (!label) {
        LOG_ERROR("WF", "lv_label_create failed for '%s' — LVGL pool OOM", cfg.id);
        lv_obj_del(cont);
        return nullptr;
    }
    const uint32_t textRgb =
        ThemeManager::getEffectiveTextColor(cfg.style.textColor.rgb, cfg.style.respectDayMode);
    lv_obj_set_style_text_color(label, lv_color_hex(ThemeManager::getStaleTextColor()), 0);
    lv_obj_set_style_text_font(label, valueFont, 0);
    lv_obj_set_style_text_letter_space(label, WidgetHelpers::valueTrackingPx(valueSize), 0);
    lv_label_set_text(label, kStalePlaceholder);

    lv_obj_t *unitLabel = nullptr;
    const char *unit = WidgetHelpers::resolveDisplayUnit(cfg.signalId, cfg.label.suffix);
    if (unit[0] != '\0') {
        unitLabel = lv_label_create(valueRow);
        if (unitLabel) {
            lv_obj_set_style_text_color(unitLabel, lv_color_hex(WidgetHelpers::kMutedRgb), 0);
            lv_obj_set_style_text_font(unitLabel, FontManager::units(), 0);
            lv_label_set_text(unitLabel, unit);
        }
    }

    const bool primary = valueSize >= WidgetHelpers::kRulePrimaryFontMin;
    const uint32_t ruleRgb = primary ? textRgb : WidgetHelpers::kTrackRgb;
    lv_obj_t *topRule = WidgetHelpers::makeTopRule(
        cont, primary ? WidgetHelpers::kRulePrimaryPx : WidgetHelpers::kRuleSecondaryPx, ruleRgb);

    lv_obj_t *barFill = nullptr;
    int16_t barMaxW = 0;
    if (cfg.label.showBar && cfg.label.maxValue > cfg.label.minValue) {
        barMaxW = static_cast<int16_t>(cfg.layout.w - 2 * kBarSideMarginPx);
        if (barMaxW > 0) {
            lv_obj_t *track = lv_obj_create(cont);
            if (track) {
                lv_obj_set_size(track, barMaxW, kBarHeightPx);
                lv_obj_align(track, LV_ALIGN_BOTTOM_LEFT, kBarSideMarginPx, 0);
                lv_obj_set_style_bg_color(track, lv_color_hex(WidgetHelpers::kTrackRgb),
                                          LV_PART_MAIN);
                lv_obj_set_style_bg_opa(track, LV_OPA_COVER, LV_PART_MAIN);
                lv_obj_set_style_border_width(track, 0, LV_PART_MAIN);
                lv_obj_set_style_pad_all(track, 0, LV_PART_MAIN);
                lv_obj_clear_flag(track, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
            }
            barFill = lv_obj_create(cont);
            if (barFill) {
                lv_obj_set_size(barFill, 1, kBarHeightPx);
                lv_obj_align(barFill, LV_ALIGN_BOTTOM_LEFT, kBarSideMarginPx, 0);
                lv_obj_set_style_bg_color(barFill, lv_color_hex(textRgb), LV_PART_MAIN);
                lv_obj_set_style_bg_opa(barFill, LV_OPA_COVER, LV_PART_MAIN);
                lv_obj_set_style_border_width(barFill, 0, LV_PART_MAIN);
                lv_obj_set_style_pad_all(barFill, 0, LV_PART_MAIN);
                lv_obj_clear_flag(barFill, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
            }
        }
    }

    WidgetTagPool::Slot<LabelTag> tagSlot;
    LabelTag *tag = tagSlot.get();
    if (!tag) {
        LOG_WARN("WF", "Tag pool exhausted for '%s' (all %u slots busy)", cfg.id,
                 static_cast<unsigned>(WidgetTagPool::kPoolSlots));
        lv_obj_del(cont);
        return nullptr;
    }
    tag->valueLabel = label;
    tag->unitLabel = unitLabel;
    tag->topRule = topRule;
    tag->kicker = kicker;
    tag->barFill = barFill;
    tag->barMaxW = barMaxW;
    tag->lastBarW = -1;
    tag->alertThreshold = cfg.label.alertThreshold;
    tag->lastValue = NAN;
    tag->lastValid = false;
    tag->baseTextRgb = textRgb;
    tag->lastTintRgb = 0xFFFFFFFFu;
    tag->ruleBaseRgb = ruleRgb;
    tag->ruleLastRgb = ruleRgb;
    tag->kickerLastRgb = WidgetLabelOverlay::kLabelDimRgb;
    tag->lastDangerActive = false;

    AlertFlash::attach(tag->alert, cont);
    AlertFlash::watchLabel(tag->alert, label, textRgb);

    lv_obj_set_user_data(cont, tag);
    lv_obj_add_event_cb(cont, WidgetTagPool::deleteHandler<LabelTag>, LV_EVENT_DELETE,
                        tagSlot.commit());

    return cont;
}

void LabelWidget::reapplyTheme(lv_obj_t *obj, const CfgWidget &cfg) {
    if (!obj)
        return;
    auto *tag = static_cast<LabelTag *>(lv_obj_get_user_data(obj));
    if (!tag || !tag->valueLabel)
        return;
    const uint32_t textRgb =
        ThemeManager::getEffectiveTextColor(cfg.style.textColor.rgb, cfg.style.respectDayMode);
    tag->baseTextRgb = textRgb;
    if (tag->barFill) {
        lv_obj_set_style_bg_color(tag->barFill, lv_color_hex(textRgb), LV_PART_MAIN);
    }
    WidgetStyles::setTextColorIfChanged(tag->valueLabel, tag->lastTintRgb, textRgb);
    if (tag->ruleBaseRgb != WidgetHelpers::kTrackRgb) {
        tag->ruleBaseRgb = textRgb;
        if (!tag->alert.active)
            setRuleColorIfChanged(tag, textRgb);
    }
}

void LabelWidget::update(lv_obj_t *obj, float value, bool valid, const CfgWidget &cfg) {
    if (!obj)
        return;
    auto *tag = static_cast<LabelTag *>(lv_obj_get_user_data(obj));
    if (!tag || !tag->valueLabel)
        return;

    const float displayValue = valid ? value : 0.0f;

    if (!valid) {
        if (tag->lastValid) {
            WidgetHelpers::setLabelTextIfChanged(tag->valueLabel, kStalePlaceholder);
            tag->lastValue = NAN;
            tag->lastValid = false;
        }
        if (!tag->alert.active) {
            const uint32_t staleRgb = ThemeManager::getStaleTextColor();
            WidgetStyles::setTextColorIfChanged(tag->valueLabel, tag->lastTintRgb, staleRgb);
        }
        if (tag->barFill && tag->lastBarW != 0) {
            lv_obj_set_width(tag->barFill, 1);
            tag->lastBarW = 0;
        }
        AlertFlash::update(tag->alert, displayValue, tag->alertThreshold);
        applyDangerState(tag);
        return;
    }

    const bool unchanged =
        tag->lastValid == valid && !std::isnan(tag->lastValue) && displayValue == tag->lastValue;
    if (!unchanged) {
        char buf[40];
        WidgetHelpers::formatValue(buf, sizeof(buf), cfg.label.prefix, cfg.label.decimalPlaces,
                                   displayValue, nullptr);
        WidgetHelpers::setLabelTextIfChanged(tag->valueLabel, buf);
        tag->lastValue = displayValue;
        tag->lastValid = valid;
    }

    if (!tag->alert.active) {
        WidgetStyles::setTextColorIfChanged(tag->valueLabel, tag->lastTintRgb, tag->baseTextRgb);
    }

    if (tag->barFill && tag->barMaxW > 0) {
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

    AlertFlash::update(tag->alert, displayValue, tag->alertThreshold);
    applyDangerState(tag);
}
