// gauge_widget.cpp — Arc-based gauge widget implementation

#include "gauge_widget.h"
#include "hardware_profile.h"
#include "diag/logger.h"

#include <lvgl.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

// Map a value to arc range (0-100 percent)
inline int32_t valueToArcPct(float value, float minVal, float maxVal) {
    if (maxVal <= minVal)
        return 0;
    float pct = (value - minVal) / (maxVal - minVal) * 100.0f;
    if (pct < 0.0f)
        pct = 0.0f;
    if (pct > 100.0f)
        pct = 100.0f;
    return static_cast<int32_t>(pct);
}

// Determine arc color based on value level
lv_color_t getArcColor(float value, const CfgWidget &cfg) {
    if (value >= cfg.gauge.dangerLevel)
        return lv_color_hex(cfg.style.criticalColor.rgb);
    if (value >= cfg.gauge.warningLevel)
        return lv_color_hex(cfg.style.warningColor.rgb);
    return lv_color_hex(cfg.style.primaryColor.rgb);
}

// Tag structure stored in LVGL user data
struct GaugeTag {
    lv_obj_t *arc;
    lv_obj_t *valueLabel;
    float lastValue;
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
    lv_obj_set_style_border_width(cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont, 0, LV_PART_MAIN);

    // Determine arc diameter (smallest of width or height, with padding)
    int32_t diam = (cfg.layout.w < cfg.layout.h ? cfg.layout.w : cfg.layout.h) - 8;
    if (diam < 40)
        diam = 40;

    // Background arc track
    lv_obj_t *arc = lv_arc_create(cont);
    lv_obj_set_size(arc, diam, diam);
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);

    // Arc range: -140° to +140° (280° sweep), 0% at bottom-left
    lv_arc_set_rotation(arc, 140);     // Start angle offset
    lv_arc_set_bg_angles(arc, 0, 280); // Background track sweep
    lv_arc_set_angles(arc, 0, 0);      // Value arc starts at 0

    // Style — background track
    lv_obj_set_style_arc_color(arc, lv_color_hex(cfg.style.secondaryColor.rgb), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 8, LV_PART_MAIN);

    // Style — value arc
    lv_obj_set_style_arc_color(arc, lv_color_hex(cfg.style.primaryColor.rgb), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 8, LV_PART_INDICATOR);

    // Remove knob
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);

    // Value label in center
    lv_obj_t *label = lv_label_create(cont);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(cfg.style.textColor.rgb), 0);

    // Choose font size based on widget height
    const lv_font_t *font = &lv_font_montserrat_20;
    if (cfg.layout.h >= 100)
        font = &lv_font_montserrat_24;
    if (cfg.layout.h >= 130)
        font = &lv_font_montserrat_32;
    lv_obj_set_style_text_font(label, font, 0);
    lv_label_set_text(label, "---");

    // Allocate and attach tag
    GaugeTag *tag = new GaugeTag{arc, label, 0.0f};
    lv_obj_set_user_data(cont, tag);

    return cont;
}

void GaugeWidget::update(lv_obj_t *obj, float value, bool valid, const CfgWidget &cfg) {
    if (!obj)
        return;

    GaugeTag *tag = static_cast<GaugeTag *>(lv_obj_get_user_data(obj));
    if (!tag)
        return;

    if (!valid) {
        // Signal invalid — show dashes, reset arc
        lv_label_set_text(tag->valueLabel, "---");
        lv_arc_set_angles(tag->arc, 0, 0);
        lv_obj_set_style_arc_color(tag->arc, lv_color_hex(cfg.style.secondaryColor.rgb),
                                   LV_PART_INDICATOR);
        return;
    }

    // Only redraw if value changed enough to avoid LVGL refresh churn
    if (value == tag->lastValue)
        return;
    tag->lastValue = value;

    // Update arc position
    int32_t pct = valueToArcPct(value, cfg.gauge.minValue, cfg.gauge.maxValue);
    int32_t angle = (pct * 280) / 100; // 280° total sweep
    lv_arc_set_angles(tag->arc, 0, static_cast<uint16_t>(angle));

    // Update arc color based on level
    lv_color_t arcColor = getArcColor(value, cfg);
    lv_obj_set_style_arc_color(tag->arc, arcColor, LV_PART_INDICATOR);

    // Update value label
    char buf[16];
    if (cfg.label.decimalPlaces == 0) {
        snprintf(buf, sizeof(buf), "%.0f", value);
    } else {
        snprintf(buf, sizeof(buf), "%.*f", cfg.label.decimalPlaces, value);
    }
    lv_label_set_text(tag->valueLabel, buf);
}
