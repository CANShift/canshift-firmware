#include "label_widget.h"
#include "diag/logger.h"
#include "ui/alert_flash.h"
#include "ui/font_manager.h"
#include "ui/sensor_color_ramp.h"
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

// 12..48 calibrated against v1 320×240 — will need ScreenProfile scaling
// when a larger panel ships (#18).
uint8_t pickValueFontSize(int16_t lineH, int16_t widgetW) {
    const int byHeight = (lineH * 65) / 100;
    const int byWidth = (widgetW * 52) / 100;
    int s = byHeight < byWidth ? byHeight : byWidth;
    if (s < 12)
        s = 12;
    if (s > 48)
        s = 48;
    return static_cast<uint8_t>(s);
}

const lv_font_t *valueFontFor(uint8_t size) {
    if (size >= 32)
        return FontManager::primary(size);
    if (size >= 20)
        return FontManager::secondary(size);
    return FontManager::label(size);
}

struct LabelTag {
    lv_obj_t *valueLabel;
    lv_obj_t *fracLabel;
    lv_obj_t *unitLabel;
    float alertThreshold;
    AlertFlash::State alert;
    // Sentinel: lastValid=false + isnan(lastValue) forces the first paint.
    float lastValue;
    bool lastValid;
    const CfgColorRamp *ramp;
    uint32_t baseTextRgb;
    // 0xFFFFFFFFu sentinel ensures the first update() paints.
    uint32_t lastTintRgb;
};

// LabelTag storage comes from the shared WidgetTagPool slab (#1031
// F-HI-2 follow-up). See ui/widgets/widget_tag_pool.h.

} // namespace

lv_obj_t *LabelWidget::create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    lv_obj_t *cont = lv_obj_create(parent);
    if (!cont) {
        // LVGL pool is exhausted (LV_USE_LOG=0 silences LVGL's own warning).
        // Bail out instead of letting the next LVGL call deref NULL and panic.
        LOG_ERROR("WF", "lv_obj_create failed for '%s' — LVGL pool OOM", cfg.id);
        return nullptr;
    }
    WidgetHelpers::initContainer(cont, cfg, yOffset, cfg.style.hasBorder,
                                 cfg.style.borderColor.rgb);

    // Auto signal header sits at the top of the cell. Font sizing intentionally
    // uses the FULL widget height — the value row is centred with a +8 px
    // downward bias (see `lv_obj_align` below) which keeps the glyph cap line
    // clear of the ~14 px header band even at the upper font tiers. Subtracting
    // the band height here would shrink the headline number on L cells from
    // primary(32) down to secondary(20–24) — the regression that surfaced when
    // the previous `hasUserLabel ? 0 : 14` branch collapsed into a uniform 14
    // alongside #1244.
    const int16_t valueLineH = cfg.layout.h;

    const lv_font_t *valueFont = valueFontFor(pickValueFontSize(valueLineH, cfg.layout.w));

    WidgetLabelOverlay::applySignalHeader(cont, cfg.signalId);

    // Layout strategy: a flex row centered horizontally holds [int][frac][unit]
    // so the three labels stay baseline-aligned with a tight gap and the whole
    // group remains centered regardless of the integer / fractional widths.
    // Before this refactor the value was a single label and decimals rendered
    // at the same font size as the integer part; users wanted AFR / voltage /
    // lambda / pressure decimals to read clearly subordinate to the headline
    // number (matches the studio preview after PR #967).
    lv_obj_t *valueRow = lv_obj_create(cont);
    if (!valueRow) {
        LOG_ERROR("WF", "valueRow create failed for '%s' — LVGL pool OOM", cfg.id);
        lv_obj_del(cont);
        return nullptr;
    }
    // Width = full widget so the flex row can centre its children
    // horizontally; height = SIZE_CONTENT so the row shrinks to the tallest
    // label and we can centre the whole row vertically inside the widget.
    lv_obj_set_size(valueRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(valueRow, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(valueRow, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(valueRow, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(valueRow, 3, LV_PART_MAIN);
    lv_obj_clear_flag(valueRow, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(valueRow, LV_FLEX_FLOW_ROW);
    // END on the cross axis keeps int / frac / unit baseline-aligned at the
    // bottom of the row — matches the visual baseline of the integer text.
    lv_obj_set_flex_align(valueRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    // Centre the row vertically in the widget. When there's an auto signal
    // header (sigHeaderH > 0) bias the row downward by half the header so
    // the value text sits at the visual centre of the FREE space below the
    // header, not the centre of the whole widget (which would clip into the
    // header). Matches the original layout users expect from before the
    // flex-row refactor.
    // Push the value row down a fixed +8 px so it sits clearly below the
    // top label band (auto-header, ~14 px tall). User feedback 2026-06-01:
    // value + unit need to be "un peu contre le bas" relative to the label.
    lv_obj_align(valueRow, LV_ALIGN_CENTER, 0, 8);

    lv_obj_t *label = lv_label_create(valueRow);
    if (!label) {
        LOG_ERROR("WF", "lv_label_create failed for '%s' — LVGL pool OOM", cfg.id);
        lv_obj_del(cont);
        return nullptr;
    }
    const uint32_t textRgb =
        ThemeManager::getEffectiveTextColor(cfg.style.textColor.rgb, cfg.style.respectDayMode);
    lv_obj_set_style_text_color(label, lv_color_hex(textRgb), 0);
    lv_obj_set_style_text_font(label, valueFont, 0);
    {
        char buf[40];
        WidgetHelpers::formatValue(buf, sizeof(buf), cfg.label.prefix, 0, 0.0f, nullptr);
        lv_label_set_text(label, buf);
    }

    // Fractional-part label (".X", ".XX", …). Created only when the widget
    // actually has decimals; for integer-only signals (RPM, speed) the label
    // is omitted so we don't burn an LVGL obj on a permanently empty slot.
    lv_obj_t *fracLabel = nullptr;
    if (cfg.label.decimalPlaces > 0) {
        fracLabel = lv_label_create(valueRow);
        if (fracLabel) {
            const uint8_t intSize = pickValueFontSize(valueLineH, cfg.layout.w);
            // ~70 % of the integer font, mirroring the studio FRAC_FONT_SCALE.
            // Clamped low so the smallest cells still get a readable size.
            uint8_t fracSize = static_cast<uint8_t>((intSize * 7) / 10);
            if (fracSize < 12)
                fracSize = 12;
            lv_obj_set_style_text_color(fracLabel, lv_color_hex(textRgb), 0);
            lv_obj_set_style_text_font(fracLabel, valueFontFor(fracSize), 0);
            lv_label_set_text(fracLabel, "");
        }
    }

    // Unit label — small grey, sits to the right of the value (frac) in the
    // same flex row so the row stays centered as the unit string changes.
    // Resolved from the bound signal's `unit` (signals.json) by default;
    // explicit `cfg.label.suffix` wins as a manual override.
    lv_obj_t *unitLabel = nullptr;
    const char *unit = WidgetHelpers::resolveDisplayUnit(cfg.signalId, cfg.label.suffix);
    if (unit[0] != '\0') {
        unitLabel = lv_label_create(valueRow);
        if (unitLabel) {
            lv_obj_set_style_text_color(unitLabel, lv_color_hex(0x888888), 0);
            lv_obj_set_style_text_font(unitLabel, FontManager::label(12), 0);
            lv_label_set_text(unitLabel, unit);
        }
    }

    // RAII slot guard (#1207): early returns from AlertFlash::attach or future
    // setup steps release the slot before LVGL takes ownership.
    WidgetTagPool::Slot<LabelTag> tagSlot;
    LabelTag *tag = tagSlot.get();
    if (!tag) {
        LOG_WARN("WF", "Tag pool exhausted for '%s' (all %u slots busy)", cfg.id,
                 static_cast<unsigned>(WidgetTagPool::kPoolSlots));
        lv_obj_del(cont);
        return nullptr;
    }
    tag->valueLabel = label;
    tag->fracLabel = fracLabel;
    tag->unitLabel = unitLabel;
    tag->alertThreshold = cfg.label.alertThreshold;
    tag->lastValue = NAN;
    tag->lastValid = false;
    tag->baseTextRgb = textRgb;
    tag->lastTintRgb = 0xFFFFFFFFu;
    tag->ramp = WidgetHelpers::resolveSignalRamp(cfg.signalId);

    AlertFlash::attach(tag->alert, cont);
    AlertFlash::watchLabel(tag->alert, label, textRgb);
    if (fracLabel)
        AlertFlash::watchLabel(tag->alert, fracLabel, textRgb);

    lv_obj_set_user_data(cont, tag);
    lv_obj_add_event_cb(cont, WidgetTagPool::deleteHandler<LabelTag>, LV_EVENT_DELETE,
                        tagSlot.commit());

    return cont;
}

// Split a formatted value buffer at the first '.' into integer and
// fractional substrings. The fractional output includes the dot itself
// (".3", ".09") so the value reads naturally when the two labels are
// concatenated visually. Either output may be empty (no fraction → frac
// is "", no integer-before-dot is impossible by construction of
// `formatValue`). Sized to the same 40-char buffers used upstream.
static void splitDecimal(const char *in, char *intOut, size_t intCap, char *fracOut,
                         size_t fracCap) {
    const char *dot = strchr(in, '.');
    if (!dot) {
        strlcpy(intOut, in, intCap);
        if (fracCap > 0)
            fracOut[0] = '\0';
        return;
    }
    const size_t intLen = static_cast<size_t>(dot - in);
    const size_t copyInt = intLen < intCap - 1 ? intLen : intCap - 1;
    memcpy(intOut, in, copyInt);
    intOut[copyInt] = '\0';
    strlcpy(fracOut, dot, fracCap);
}

void LabelWidget::reapplyTheme(lv_obj_t *obj, const CfgWidget &cfg) {
    if (!obj)
        return;
    auto *tag = static_cast<LabelTag *>(lv_obj_get_user_data(obj));
    if (!tag || !tag->valueLabel)
        return;
    const uint32_t textRgb =
        ThemeManager::getEffectiveTextColor(cfg.style.textColor.rgb, cfg.style.respectDayMode);
    // Refresh the static base colour so the next update() sees the new theme
    // when no ramp / palette override applies. Push the same colour straight
    // through the `setTextColorIfChanged` cache so the static "0" placeholder
    // visible while the signal is invalid repaints immediately, without
    // waiting for a signal-driven tick.
    tag->baseTextRgb = textRgb;
    WidgetStyles::setTextColorIfChanged(tag->valueLabel, tag->lastTintRgb, textRgb);
    if (tag->fracLabel) {
        uint32_t fracLast = tag->lastTintRgb;
        WidgetStyles::setTextColorIfChanged(tag->fracLabel, fracLast, textRgb);
    }
}

void LabelWidget::update(lv_obj_t *obj, float value, bool valid, const CfgWidget &cfg) {
    if (!obj)
        return;
    auto *tag = static_cast<LabelTag *>(lv_obj_get_user_data(obj));
    if (!tag || !tag->valueLabel)
        return;

    // Invalid signals render as "0" so the widget stays visible at all times
    // (issue #1243) — matches the Studio preview which never blanks. The
    // previous `hideWhenInvalid` opt-in was dropped in schema 1.20 because
    // users mistook a hidden voltage / pressure readout for a flashing bug.
    const float displayValue = valid ? value : 0.0f;

    // Skip snprintf + lv_label_set_text when nothing has changed. The text
    // formatting is purely a function of (displayValue, valid) — same inputs
    // mean an identical buffer, so the realloc is pure waste.
    const bool unchanged =
        tag->lastValid == valid && !std::isnan(tag->lastValue) && displayValue == tag->lastValue;
    if (!unchanged) {
        char buf[40];
        WidgetHelpers::formatValue(buf, sizeof(buf), cfg.label.prefix, cfg.label.decimalPlaces,
                                   displayValue, nullptr);
        if (tag->fracLabel) {
            char intPart[24];
            char fracPart[16];
            splitDecimal(buf, intPart, sizeof(intPart), fracPart, sizeof(fracPart));
            WidgetHelpers::setLabelTextIfChanged(tag->valueLabel, intPart);
            WidgetHelpers::setLabelTextIfChanged(tag->fracLabel, fracPart);
        } else {
            WidgetHelpers::setLabelTextIfChanged(tag->valueLabel, buf);
        }
        tag->lastValue = displayValue;
        tag->lastValid = valid;
    }

    // Tint the value when a ramp is configured (issue #430). Skip during an
    // alert flash — AlertFlash owns the colour for the duration of the pulse.
    if (tag->ramp && !tag->alert.active) {
        const uint32_t tint = valid ? colorAtValue(*tag->ramp, value) : tag->baseTextRgb;
        WidgetStyles::setTextColorIfChanged(tag->valueLabel, tag->lastTintRgb, tint);
        if (tag->fracLabel) {
            // Re-use the same cached tint so both labels track in lockstep.
            uint32_t fracLast = tag->lastTintRgb;
            WidgetStyles::setTextColorIfChanged(tag->fracLabel, fracLast, tint);
        }
    }

    // Drive the threshold flash from the live value (NaN threshold = disabled).
    AlertFlash::update(tag->alert, displayValue, tag->alertThreshold);
}
