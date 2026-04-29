// bar_widget.cpp — Horizontal / vertical progress bar widget implementation

#include "bar_widget.h"
#include "diag/logger.h"

#include <lvgl.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

    // lv_bar only accepts int32_t ranges. Multiplying by this factor gives
    // 2 decimal places of resolution for fractional signals (boost, lambda, etc.)
    // while keeping all scaled values well within int32_t bounds even for
    // large-range signals (RPM 0–8000 → 0–800 000).
    static constexpr int32_t BAR_SCALE = 100;

    // Scale a float signal value to the integer range used by lv_bar.
    inline int32_t scaleValue(float value, float minVal, float maxVal) {
        float clamped = value < minVal ? minVal : (value > maxVal ? maxVal : value);
        return static_cast<int32_t>(clamped * BAR_SCALE);
    }

    // Determine bar indicator color based on value thresholds.
    lv_color_t getBarColor(float value, const CfgWidget& cfg) {
        if (value >= cfg.bar.dangerLevel)  return lv_color_hex(cfg.style.criticalColor.rgb);
        if (value >= cfg.bar.warningLevel) return lv_color_hex(cfg.style.warningColor.rgb);
        return lv_color_hex(cfg.style.primaryColor.rgb);
    }

    // Tag structure stored in LVGL user data.
    struct BarTag {
        lv_obj_t* bar;
        lv_obj_t* valueLabel;   // nullptr on very small widgets
        float     lastValue;
        bool      wasValid;
    };

    // Minimum widget dimension thresholds to show a value label.
    static constexpr int16_t LABEL_MIN_H_HORIZ = 36;  // px — horizontal bar must be at least this tall
    static constexpr int16_t LABEL_MIN_W_VERT  = 36;  // px — vertical bar must be at least this wide

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

lv_obj_t* BarWidget::create(lv_obj_t* parent, const CfgWidget& cfg, int16_t yOffset) {
    const bool isVertical = cfg.bar.isVertical;

    // Container
    lv_obj_t* cont = lv_obj_create(parent);
    lv_obj_set_pos(cont, cfg.layout.x, cfg.layout.y + yOffset);
    lv_obj_set_size(cont, cfg.layout.w, cfg.layout.h);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont, 0, LV_PART_MAIN);

    // LVGL bar — fill the full container area
    lv_obj_t* bar = lv_bar_create(cont);
    lv_obj_set_size(bar, cfg.layout.w, cfg.layout.h);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);

    // Set scaled integer range — BAR_SCALE gives 0.01 resolution on fractional signals
    lv_bar_set_range(bar,
        static_cast<int32_t>(cfg.bar.minValue * BAR_SCALE),
        static_cast<int32_t>(cfg.bar.maxValue * BAR_SCALE));
    lv_bar_set_value(bar, static_cast<int32_t>(cfg.bar.minValue * BAR_SCALE), LV_ANIM_OFF);

    // Background track style
    lv_obj_set_style_bg_color(bar, lv_color_hex(cfg.style.secondaryColor.rgb), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 2, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);

    // Indicator (filled portion) style
    lv_obj_set_style_bg_color(bar, lv_color_hex(cfg.style.primaryColor.rgb), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 2, LV_PART_INDICATOR);

    // Value label — only if widget has enough room
    lv_obj_t* valueLabel = nullptr;
    bool hasRoom = isVertical
        ? (cfg.layout.w >= LABEL_MIN_W_VERT)
        : (cfg.layout.h >= LABEL_MIN_H_HORIZ);

    if (hasRoom) {
        valueLabel = lv_label_create(cont);
        lv_obj_set_style_text_color(valueLabel, lv_color_hex(cfg.style.textColor.rgb), 0);
        lv_obj_set_style_text_font(valueLabel, &lv_font_montserrat_14, 0);
        lv_obj_align(valueLabel, LV_ALIGN_CENTER, 0, 0);
        lv_label_set_text(valueLabel, "---");
    }

    // Attach tag
    BarTag* tag = new BarTag{bar, valueLabel, cfg.bar.minValue, false};
    lv_obj_set_user_data(cont, tag);

    LOG_DEBUG("BAR", "Created %s bar at (%d,%d) size=%dx%d range=[%.0f,%.0f]",
              isVertical ? "vertical" : "horizontal",
              cfg.layout.x, cfg.layout.y + yOffset,
              cfg.layout.w, cfg.layout.h,
              cfg.bar.minValue, cfg.bar.maxValue);

    return cont;
}

void BarWidget::update(lv_obj_t* obj, float value, bool valid, const CfgWidget& cfg) {
    if (!obj) return;

    BarTag* tag = static_cast<BarTag*>(lv_obj_get_user_data(obj));
    if (!tag) return;

    if (!valid) {
        if (tag->wasValid) {
            // Signal just went invalid — reset to minimum and dim
            lv_bar_set_value(tag->bar,
                static_cast<int32_t>(cfg.bar.minValue * BAR_SCALE), LV_ANIM_OFF);
            lv_obj_set_style_bg_color(tag->bar,
                lv_color_hex(cfg.style.secondaryColor.rgb), LV_PART_INDICATOR);
            if (tag->valueLabel) {
                lv_label_set_text(tag->valueLabel, "---");
            }
            tag->wasValid = false;
        }
        return;
    }

    // Skip redundant update
    if (valid == tag->wasValid && value == tag->lastValue) return;
    tag->lastValue = value;
    tag->wasValid  = true;

    // Update bar position — scaled to match the range set in create()
    lv_bar_set_value(tag->bar, scaleValue(value, cfg.bar.minValue, cfg.bar.maxValue), LV_ANIM_OFF);

    // Update indicator color based on threshold
    lv_color_t color = getBarColor(value, cfg);
    lv_obj_set_style_bg_color(tag->bar, color, LV_PART_INDICATOR);

    // Update value label
    if (tag->valueLabel) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.0f", value);
        lv_label_set_text(tag->valueLabel, buf);
        lv_obj_set_style_text_color(tag->valueLabel, color, 0);
    }
}
