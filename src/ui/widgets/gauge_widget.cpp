// gauge_widget.cpp — Arc-based gauge widget with colored zone sectors

#include "gauge_widget.h"
#include "ui/alert_flash.h"
#include "ui/font_manager.h"
#include "ui/theme_manager.h"
#include "ui/widget_label.h"
#include "hardware_profile.h"
#include "diag/logger.h"

#include <cmath>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

// Palette for the colored arc sectors
static constexpr uint32_t kColorSuccess = 0x00CC44; // Green — normal range
static constexpr uint32_t kColorWarning = 0xFF8800; // Orange — warning range
static constexpr uint32_t kColorDanger = 0xFF4444;  // Red — danger range
static constexpr uint32_t kColorBgDim = 0x222222;   // Dark grey — no-threshold bg track
static constexpr uint32_t kColorValue = 0xFFFFFF;   // White — value indicator needle

// Arc sweep constants (matches rotation=140, bg_angles=0..280)
static constexpr float kArcSweep = 280.0f;

// Map a signal value to arc angle [0..280]
static uint16_t valueToAngle(float value, float minVal, float maxVal) {
    if (maxVal <= minVal)
        return 0;
    float pct = (value - minVal) / (maxVal - minVal);
    if (pct < 0.0f)
        pct = 0.0f;
    if (pct > 1.0f)
        pct = 1.0f;
    return static_cast<uint16_t>(pct * kArcSweep);
}

// Create a background-only arc covering [startAngle..endAngle] in the given color.
// The indicator portion is fully transparent — only the track ring is visible.
static lv_obj_t *createSectorArc(lv_obj_t *parent, int32_t diam, uint16_t startAngle,
                                 uint16_t endAngle, uint32_t colorRgb, uint8_t arcWidth) {
    if (startAngle >= endAngle)
        return nullptr;

    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, diam, diam);
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_rotation(arc, 140);
    lv_arc_set_bg_angles(arc, startAngle, endAngle);
    lv_arc_set_angles(arc, 0, 0); // No indicator

    // Background track: colored zone — square ends per dashboard styling.
    lv_obj_set_style_arc_color(arc, lv_color_hex(colorRgb), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, arcWidth, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);

    // Indicator: invisible
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, arcWidth, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);

    // No knob
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);

    return arc;
}

// Create the value indicator arc (transparent bg, bright indicator on top).
// Rendered last so it draws above the sector layers.
static lv_obj_t *createValueArc(lv_obj_t *parent, int32_t diam, uint8_t indicatorWidth) {
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, diam, diam);
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_rotation(arc, 140);
    lv_arc_set_bg_angles(arc, 0, 280);
    lv_arc_set_angles(arc, 0, 0);

    // Background: transparent (sector arcs show through)
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);

    // Indicator: bright white needle — square ends.
    lv_obj_set_style_arc_color(arc, lv_color_hex(kColorValue), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, indicatorWidth, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);

    // No knob
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);

    return arc;
}

// Tag structure stored in LVGL user data
struct GaugeTag {
    lv_obj_t *valueLabel;
    lv_obj_t *unitLabel;
    float minValue;
    float maxValue;
    float lastValue;
    float alertThreshold; // NaN = disabled (issue #133)
    // Threshold angles (0 = not set)
    uint16_t warnAngle;
    uint16_t dangerAngle;
    bool hasWarning;
    bool hasDanger;
    bool lastValid; // Tracks the last (value, valid) pair so the invalid
                    // branch can skip its lv_label_set_text reallocation
                    // when state hasn't flipped (issue #236).
    AlertFlash::State alert;
};

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

lv_obj_t *GaugeWidget::create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    // Container
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_pos(cont, cfg.layout.x, cfg.layout.y + yOffset);
    lv_obj_set_size(cont, cfg.layout.w, cfg.layout.h);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, LV_PART_MAIN);
    if (cfg.style.hasBorder) {
        lv_obj_set_style_border_width(cont, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(cont, lv_color_hex(cfg.style.borderColor.rgb), LV_PART_MAIN);
    } else {
        lv_obj_set_style_border_width(cont, 0, LV_PART_MAIN);
    }
    lv_obj_set_style_pad_all(cont, 0, LV_PART_MAIN);

    // Arc diameter: smallest of w/h, minus padding
    int32_t diam = (cfg.layout.w < cfg.layout.h ? cfg.layout.w : cfg.layout.h) - 8;
    if (diam < 40)
        diam = 40;

    // Determine which threshold zones apply.
    // Convention: warningLevel > minValue means a real threshold is configured.
    const float minV = cfg.gauge.minValue;
    const float maxV = cfg.gauge.maxValue;
    const bool hasWarning = (cfg.gauge.warningLevel > minV && cfg.gauge.warningLevel < maxV);
    const bool hasDanger = hasWarning && (cfg.gauge.dangerLevel > cfg.gauge.warningLevel &&
                                          cfg.gauge.dangerLevel <= maxV);

    const uint16_t warnAngle = hasWarning ? valueToAngle(cfg.gauge.warningLevel, minV, maxV) : 0;
    const uint16_t dangerAngle = hasDanger ? valueToAngle(cfg.gauge.dangerLevel, minV, maxV) : 0;

    // Arc line widths — thick enough to read on the now-smaller (h=80)
    // dashboard arcs without overwhelming the value text in the centre.
    constexpr uint8_t kBgWidth = 14; // Sector track width
    constexpr uint8_t kIndWidth = 7; // Value indicator (thinner, sits inside)

    // ---------------------------------------------------------------------------
    // Layer 1-3: Static sector arcs (background track, colored zones)
    // Render order: first created = bottom, last created = top.
    // ---------------------------------------------------------------------------

    if (!hasWarning) {
        // No thresholds — single dimmed background track (old behavior)
        createSectorArc(cont, diam, 0, 280, kColorBgDim, kBgWidth);
    } else if (!hasDanger) {
        // Two zones: green (0→warn) + orange (warn→280)
        createSectorArc(cont, diam, 0, warnAngle, kColorSuccess, kBgWidth);
        createSectorArc(cont, diam, warnAngle, 280, kColorWarning, kBgWidth);
    } else {
        // Three zones: green (0→warn), orange (warn→danger), red (danger→280)
        createSectorArc(cont, diam, 0, warnAngle, kColorSuccess, kBgWidth);
        createSectorArc(cont, diam, warnAngle, dangerAngle, kColorWarning, kBgWidth);
        createSectorArc(cont, diam, dangerAngle, 280, kColorDanger, kBgWidth);
    }

    // The white indicator needle was dropped per user spec — the coloured
    // sector arcs + the centred numeric value carry the read on their own.

    // ---------------------------------------------------------------------------
    // Value label (centered inside the arc)
    // ---------------------------------------------------------------------------

    bool hasUnit = cfg.gauge.suffix[0] != '\0';
    const uint32_t textRgb = ThemeManager::getEffectiveTextColor();
    lv_obj_t *label = lv_label_create(cont);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, hasUnit ? -8 : 0);
    lv_obj_set_style_text_color(label, lv_color_hex(textRgb), 0);

    const lv_font_t *font = FontManager::get(20);
    if (cfg.layout.h >= 100)
        font = FontManager::get(24);
    if (cfg.layout.h >= 130)
        font = FontManager::get(32);
    lv_obj_set_style_text_font(label, font, 0);
    {
        // Initial readout: 0, formatted to the configured decimalPlaces with
        // optional prefix — matches the "no signal yet → show 0" rule.
        char initBuf[24];
        snprintf(initBuf, sizeof(initBuf), "%s%.*f", cfg.gauge.prefix, cfg.gauge.decimalPlaces,
                 0.0f);
        lv_label_set_text(label, initBuf);
    }

    // Unit label below value (optional)
    lv_obj_t *unitLabel = nullptr;
    if (hasUnit) {
        unitLabel = lv_label_create(cont);
        lv_obj_align(unitLabel, LV_ALIGN_CENTER, 0, 12);
        lv_obj_set_style_text_color(unitLabel, lv_color_hex(textRgb & 0x888888), 0);
        lv_obj_set_style_text_font(unitLabel, FontManager::get(12), 0);
        lv_label_set_text(unitLabel, cfg.gauge.suffix);
    }

    // Optional widget label drawn at the configured corner.
    WidgetLabelOverlay::apply(cont, cfg.gauge.label, cfg.gauge.labelPosition, textRgb);

    // Allocate and attach tag
    GaugeTag *tag = new GaugeTag{};
    tag->valueLabel = label;
    tag->unitLabel = unitLabel;
    tag->minValue = minV;
    tag->maxValue = maxV;
    // NaN sentinel — guarantees the first update() runs through the paint
    // path even when the live value happens to be 0.0 (matches bar_widget).
    tag->lastValue = NAN;
    tag->alertThreshold = cfg.gauge.alertThreshold;
    tag->warnAngle = warnAngle;
    tag->dangerAngle = dangerAngle;
    tag->hasWarning = hasWarning;
    tag->hasDanger = hasDanger;
    tag->lastValid = true;

    // Mount the alert overlay last so it sits on top of arcs and labels.
    AlertFlash::attach(tag->alert, cont);
    AlertFlash::watchLabel(tag->alert, label, textRgb);
    if (unitLabel) {
        AlertFlash::watchLabel(tag->alert, unitLabel, textRgb & 0x888888);
    }

    lv_obj_set_user_data(cont, tag);

    lv_obj_add_event_cb(
        cont,
        [](lv_event_t *e) {
            auto *t = static_cast<GaugeTag *>(lv_event_get_user_data(e));
            delete t;
        },
        LV_EVENT_DELETE, tag);

    return cont;
}

void GaugeWidget::update(lv_obj_t *obj, float value, bool valid, const CfgWidget &cfg) {
    if (!obj)
        return;

    GaugeTag *tag = static_cast<GaugeTag *>(lv_obj_get_user_data(obj));
    if (!tag)
        return;

    const float displayValue = valid ? value : 0.0f;

    if (!valid) {
        // No live signal → display 0 (formatted) instead of placeholder dashes.
        // Skip the realloc when we're already showing the invalid readout
        // (issue #236) — the formatted "0" buffer is identical every tick.
        if (tag->lastValid || !std::isnan(tag->lastValue) || tag->lastValue != 0.0f) {
            char buf[24];
            snprintf(buf, sizeof(buf), "%s%.*f", cfg.gauge.prefix, cfg.gauge.decimalPlaces, 0.0f);
            const char *current = lv_label_get_text(tag->valueLabel);
            if (current == nullptr || strcmp(current, buf) != 0) {
                lv_label_set_text(tag->valueLabel, buf);
            }
            tag->lastValue = 0.0f;
            tag->lastValid = false;
        }
        if (!tag->alert.active) {
            lv_obj_set_style_text_color(tag->valueLabel,
                                        lv_color_hex(ThemeManager::getEffectiveTextColor()), 0);
        }
        AlertFlash::update(tag->alert, displayValue, tag->alertThreshold);
        return;
    }

    // Only redraw if value (or valid flag) changed to avoid LVGL refresh churn.
    if (tag->lastValid && value == tag->lastValue) {
        AlertFlash::update(tag->alert, displayValue, tag->alertThreshold);
        return;
    }
    tag->lastValue = value;
    tag->lastValid = true;

    // Tint the value label to match the active zone — but skip when in alert
    // state, AlertFlash owns the colour while the flash is active.
    if (!tag->alert.active) {
        uint32_t labelColor = ThemeManager::getEffectiveTextColor();
        if (tag->hasDanger && value >= cfg.gauge.dangerLevel)
            labelColor = kColorDanger;
        else if (tag->hasWarning && value >= cfg.gauge.warningLevel)
            labelColor = kColorWarning;
        lv_obj_set_style_text_color(tag->valueLabel, lv_color_hex(labelColor), 0);
    }

    // Update numeric label (prefix + value formatted to decimalPlaces). Even
    // with a different float value, formatting may collapse to the same string
    // (e.g. 78.001 vs 78.004 at 0 dp) — strcmp guard avoids the realloc.
    char buf[24];
    snprintf(buf, sizeof(buf), "%s%.*f", cfg.gauge.prefix, cfg.gauge.decimalPlaces, value);
    const char *current = lv_label_get_text(tag->valueLabel);
    if (current == nullptr || strcmp(current, buf) != 0) {
        lv_label_set_text(tag->valueLabel, buf);
    }

    // Drive the threshold flash from the live value (NaN threshold = disabled).
    AlertFlash::update(tag->alert, displayValue, tag->alertThreshold);
}
