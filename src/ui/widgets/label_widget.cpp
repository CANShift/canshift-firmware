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
    lv_obj_t *valueLabel;
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

    lv_obj_t *label = lv_label_create(cont);
    if (!label) {
        LOG_ERROR("WF", "lv_label_create failed for '%s' — LVGL pool OOM", cfg.id);
        lv_obj_del(cont);
        return nullptr;
    }
    const uint32_t textRgb =
        ThemeManager::getEffectiveTextColor(cfg.style.textColor.rgb, cfg.style.respectDayMode);
    lv_obj_set_style_text_color(label, lv_color_hex(textRgb), 0);
    lv_obj_set_style_text_font(label, valueFont, 0);

    // Suffix is rendered inline with the value when shown ("78°C"), but is
    // dropped entirely when a user label is set — the label already conveys
    // the unit (e.g. "COOLANT" → °C is implicit). Frees screen real estate.
    const int16_t valueYOffset = static_cast<int16_t>(sigHeaderH / 2);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, valueYOffset);

    {
        // Numeric widgets render the value bare ("78", "195"). The unit
        // (°C, km/h, …) is conveyed by the label itself — repeating it on
        // the value just clips wide numbers ("195km/h" → "195k") and adds
        // visual noise. Arc and bar widgets keep their suffix.
        char buf[40];
        WidgetHelpers::formatValue(buf, sizeof(buf), cfg.label.prefix, cfg.label.decimalPlaces,
                                   0.0f, nullptr);
        lv_label_set_text(label, buf);
    }

    if (hasUserLabel) {
        WidgetLabelOverlay::apply(cont, cfg.label.label, cfg.label.labelPosition, textRgb);
    }

    auto *tag = new LabelTag{};
    tag->valueLabel = label;
    tag->alertThreshold = cfg.label.alertThreshold;
    tag->lastValue = NAN;
    tag->lastValid = false;
    tag->baseTextRgb = textRgb;
    tag->lastTintRgb = 0xFFFFFFFFu;
    tag->ramp = WidgetHelpers::resolveSignalRamp(cfg.signalId);

    AlertFlash::attach(tag->alert, cont);
    AlertFlash::watchLabel(tag->alert, label, textRgb);

    WidgetHelpers::attachTagDeleter(cont, tag);

    return cont;
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
        // Suffix dropped unconditionally — the label conveys the unit. Wide
        // numeric values ("195km/h" → "195k") were getting clipped on the right
        // edge of 80-px-wide widgets. See create() for full rationale.
        char buf[40];
        WidgetHelpers::formatValue(buf, sizeof(buf), cfg.label.prefix, cfg.label.decimalPlaces,
                                   displayValue, nullptr);
        WidgetHelpers::setLabelTextIfChanged(tag->valueLabel, buf);
        tag->lastValue = displayValue;
        tag->lastValid = valid;
    }

    // Tint the value when a ramp is configured (issue #430). Skip during an
    // alert flash — AlertFlash owns the colour for the duration of the pulse.
    if (tag->ramp && !tag->alert.active) {
        const uint32_t tint = valid ? colorAtValue(*tag->ramp, value) : tag->baseTextRgb;
        WidgetStyles::setTextColorIfChanged(tag->valueLabel, tag->lastTintRgb, tint);
    }

    // Drive the threshold flash from the live value (NaN threshold = disabled).
    AlertFlash::update(tag->alert, displayValue, tag->alertThreshold);
}
