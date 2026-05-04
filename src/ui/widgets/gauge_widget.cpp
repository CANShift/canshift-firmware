// gauge_widget.cpp — Arc-based gauge widget with colored zone sectors

#include "gauge_widget.h"
#include "ui/font_manager.h"
#include "hardware_profile.h"
#include "diag/logger.h"

#include <lvgl.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

// Palette for the colored arc sectors
static constexpr uint32_t kColorSuccess = 0x00CC44; // Green — normal range
static constexpr uint32_t kColorWarning = 0xFF8800; // Orange — warning range
static constexpr uint32_t kColorDanger  = 0xFF4444; // Red — danger range
static constexpr uint32_t kColorBgDim   = 0x222222; // Dark grey — no-threshold bg track
static constexpr uint32_t kColorValue   = 0xFFFFFF; // White — value indicator needle

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
static lv_obj_t *createSectorArc(lv_obj_t *parent, int32_t diam,
                                  uint16_t startAngle, uint16_t endAngle,
                                  uint32_t colorRgb, uint8_t arcWidth) {
    if (startAngle >= endAngle)
        return nullptr;

    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, diam, diam);
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_rotation(arc, 140);
    lv_arc_set_bg_angles(arc, startAngle, endAngle);
    lv_arc_set_angles(arc, 0, 0); // No indicator

    // Background track: colored zone
    lv_obj_set_style_arc_color(arc, lv_color_hex(colorRgb), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, arcWidth, LV_PART_MAIN);

    // Indicator: invisible
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, arcWidth, LV_PART_INDICATOR);

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

    // Indicator: bright white needle
    lv_obj_set_style_arc_color(arc, lv_color_hex(kColorValue), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, indicatorWidth, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);

    // No knob
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);

    return arc;
}

// Tag structure stored in LVGL user data
struct GaugeTag {
    lv_obj_t *arcValue; // White indicator needle (always present)
    lv_obj_t *valueLabel;
    lv_obj_t *unitLabel;
    float minValue;
    float maxValue;
    float lastValue;
    // Threshold angles (0 = not set)
    uint16_t warnAngle;
    uint16_t dangerAngle;
    bool hasWarning;
    bool hasDanger;
    bool showNeedle;
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
    const bool hasWarning =
        (cfg.gauge.warningLevel > minV && cfg.gauge.warningLevel < maxV);
    const bool hasDanger =
        hasWarning &&
        (cfg.gauge.dangerLevel > cfg.gauge.warningLevel && cfg.gauge.dangerLevel <= maxV);

    const uint16_t warnAngle =
        hasWarning ? valueToAngle(cfg.gauge.warningLevel, minV, maxV) : 0;
    const uint16_t dangerAngle =
        hasDanger ? valueToAngle(cfg.gauge.dangerLevel, minV, maxV) : 0;

    // Arc line widths
    constexpr uint8_t kBgWidth  = 8; // Sector track width
    constexpr uint8_t kIndWidth = 4; // Value indicator width (thinner, sits inside)

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

    // ---------------------------------------------------------------------------
    // Layer 4: Value indicator (white, thinner, on top of sector colors)
    // ---------------------------------------------------------------------------

    lv_obj_t *arcValue = createValueArc(cont, diam, kIndWidth);

    // ---------------------------------------------------------------------------
    // Value label (centered inside the arc)
    // ---------------------------------------------------------------------------

    bool hasUnit = cfg.gauge.suffix[0] != '\0';
    lv_obj_t *label = lv_label_create(cont);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, hasUnit ? -8 : 0);
    lv_obj_set_style_text_color(label, lv_color_hex(cfg.style.textColor.rgb), 0);

    const lv_font_t *font = FontManager::get(20);
    if (cfg.layout.h >= 100)
        font = FontManager::get(24);
    if (cfg.layout.h >= 130)
        font = FontManager::get(32);
    lv_obj_set_style_text_font(label, font, 0);
    lv_label_set_text(label, "---");

    // Unit label below value (optional)
    lv_obj_t *unitLabel = nullptr;
    if (hasUnit) {
        unitLabel = lv_label_create(cont);
        lv_obj_align(unitLabel, LV_ALIGN_CENTER, 0, 12);
        lv_obj_set_style_text_color(unitLabel, lv_color_hex(cfg.style.textColor.rgb & 0x888888),
                                    0);
        lv_obj_set_style_text_font(unitLabel, FontManager::get(12), 0);
        lv_label_set_text(unitLabel, cfg.gauge.suffix);
    }

    // Allocate and attach tag
    GaugeTag *tag = new GaugeTag{arcValue, label, unitLabel,
                                  minV, maxV, 0.0f,
                                  warnAngle, dangerAngle,
                                  hasWarning, hasDanger,
                                  cfg.gauge.showNeedle};
    lv_obj_set_user_data(cont, tag);

    lv_obj_add_event_cb(cont, [](lv_event_t* e) {
        auto* t = static_cast<GaugeTag*>(lv_event_get_user_data(e));
        delete t;
    }, LV_EVENT_DELETE, tag);

    return cont;
}

void GaugeWidget::update(lv_obj_t *obj, float value, bool valid, const CfgWidget &cfg) {
    if (!obj)
        return;

    GaugeTag *tag = static_cast<GaugeTag *>(lv_obj_get_user_data(obj));
    if (!tag)
        return;

    if (!valid) {
        lv_label_set_text(tag->valueLabel, "---");
        lv_arc_set_angles(tag->arcValue, 0, 0);
        // Dim the indicator when invalid
        lv_obj_set_style_arc_opa(tag->arcValue, LV_OPA_40, LV_PART_INDICATOR);
        return;
    }

    // Only redraw if value changed to avoid LVGL refresh churn
    if (value == tag->lastValue)
        return;
    tag->lastValue = value;

    // Restore full opacity (was dimmed when invalid)
    lv_obj_set_style_arc_opa(tag->arcValue, LV_OPA_COVER, LV_PART_INDICATOR);

    // Advance value indicator to current position
    uint16_t angle = valueToAngle(value, tag->minValue, tag->maxValue);
    if (tag->showNeedle) {
        // Needle mode: thin 4-degree arc tip at the current angle
        const uint16_t half = 2;
        const uint16_t start = angle > half ? angle - half : 0;
        const uint16_t end = angle + half < static_cast<uint16_t>(kArcSweep)
                             ? angle + half
                             : static_cast<uint16_t>(kArcSweep);
        lv_arc_set_angles(tag->arcValue, start, end);
    } else {
        lv_arc_set_angles(tag->arcValue, 0, angle);
    }

    // Tint the value label to match the active zone
    uint32_t labelColor = cfg.style.textColor.rgb;
    if (tag->hasDanger && value >= cfg.gauge.dangerLevel)
        labelColor = kColorDanger;
    else if (tag->hasWarning && value >= cfg.gauge.warningLevel)
        labelColor = kColorWarning;
    lv_obj_set_style_text_color(tag->valueLabel, lv_color_hex(labelColor), 0);

    // Update numeric label
    char buf[16];
    if (cfg.label.decimalPlaces == 0) {
        snprintf(buf, sizeof(buf), "%.0f", value);
    } else {
        snprintf(buf, sizeof(buf), "%.*f", cfg.label.decimalPlaces, value);
    }
    lv_label_set_text(tag->valueLabel, buf);
}
