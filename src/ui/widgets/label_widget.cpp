// label_widget.cpp — Numeric value widget (displayStyle=numeric).
//
// Layout:
//   ┌────────────────────────────┐
//   │ COOLANT TEMP C  ← header   │  (auto signal name when no user label)
//   │                            │
//   │         78°C               │  (value + suffix concatenated, coloured)
//   │                            │
//   │ COOLANT  ← user label      │  (rendered at cfg.labelPosition when set)
//   └────────────────────────────┘
// Header and user label are mutually exclusive. Suffix sits inline with the
// value so a corner-anchored user label can never collide with the unit.

#include "label_widget.h"
#include "diag/logger.h"
#include "ui/alert_flash.h"
#include "ui/font_manager.h"
#include "ui/sensor_color_ramp.h"
#include "ui/theme_manager.h"
#include "ui/widget_label.h"
#include "ui/widget_styles.h"
#include "ui/widgets/widget_helpers.h"
#include <cmath>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

namespace {

// Scale the value font to the available band, capped to 52 % of widget width
// so a wide number ("12345") never overflows. Snapped by FontManager to a
// cached size in the appropriate Orbitron tier.
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

// Route a numeric size to the right Orbitron weight. Primary (Black) for the
// large value bands, secondary (Bold) mid-range, label (Medium) for the small
// auto-fit cells where the band is too narrow for a heavy weight.
const lv_font_t *valueFontFor(uint8_t size) {
    if (size >= 32)
        return FontManager::primary(size);
    if (size >= 20)
        return FontManager::secondary(size);
    return FontManager::label(size);
}

struct LabelTag {
    lv_obj_t *valueLabel; // Integer-part label (left of decimal point).
    lv_obj_t *fracLabel;  // Fractional-part label ".X" at ~70 % of the
                          // integer font. nullptr when decimalPlaces==0.
    lv_obj_t *unitLabel;  // Optional small grey suffix anchored to the
                          // value baseline. nullptr when no suffix.
    float alertThreshold; // NaN = disabled (issue #133)
    AlertFlash::State alert;
    // Cached numeric value & validity — short-circuits the per-tick snprintf
    // and lv_label_set_text reallocation when nothing has changed (issue #236).
    // Sentinel: lastValid=false + isnan(lastValue) forces the first paint.
    float lastValue;
    bool lastValid;
    // Active color ramp (issue #430). When non-null, the value text is tinted
    // from the ramp on each update; otherwise the static text colour stays.
    const CfgColorRamp *ramp;
    uint32_t baseTextRgb; // Base/static text colour when no ramp applies.
    // Cached tint pushed to LVGL — guards skip the redundant style write when
    // the ramp resolves to the same colour two ticks in a row. Init to
    // 0xFFFFFFFFu so the first update() always paints.
    uint32_t lastTintRgb;
};

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

    const bool hasUserLabel = cfg.label.label[0] != '\0';

    // Auto signal header eats ~14 px from the top — only when no user label.
    const int16_t sigHeaderH = hasUserLabel ? 0 : 14;
    const int16_t valueLineH = cfg.layout.h - sigHeaderH;

    const lv_font_t *valueFont = valueFontFor(pickValueFontSize(valueLineH, cfg.layout.w));

    if (!hasUserLabel) {
        WidgetLabelOverlay::applySignalHeader(cont, cfg.signalId);
    }

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
    lv_obj_align(valueRow, LV_ALIGN_CENTER, 0, static_cast<int16_t>(sigHeaderH / 2));

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

    if (hasUserLabel) {
        WidgetLabelOverlay::apply(cont, cfg.label.label, cfg.label.labelPosition, textRgb);
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

    auto *tag = new LabelTag{};
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

    WidgetHelpers::attachTagDeleter(cont, tag);

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

void LabelWidget::update(lv_obj_t *obj, float value, bool valid, const CfgWidget &cfg) {
    if (!obj)
        return;
    auto *tag = static_cast<LabelTag *>(lv_obj_get_user_data(obj));
    if (!tag || !tag->valueLabel)
        return;

    if (!valid && cfg.label.hideWhenInvalid) {
        // Only retext when state actually flips — lv_label_set_text always
        // reallocates and invalidates the label area (issue #236).
        if (tag->lastValid || !std::isnan(tag->lastValue)) {
            lv_label_set_text(tag->valueLabel, "");
            if (tag->fracLabel)
                lv_label_set_text(tag->fracLabel, "");
            tag->lastValid = false;
            tag->lastValue = NAN;
        }
        AlertFlash::update(tag->alert, 0.0f, tag->alertThreshold);
        return;
    }

    // Invalid signals fall through with value=0 — same formatting as live
    // values so the dashboard always reads numerically.
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
