// label_widget.cpp — Numeric value widget (displayStyle=numeric).
//
// Layout mirrors studio's GaugeNumericPreview:
//   ┌────────────────────────────┐
//   │ COOLANT TEMP C  ← header   │  (auto signal name when no user label)
//   │                            │
//   │          78                │  (large coloured value, prefix included)
//   │          °C                │  (suffix on its own dim line)
//   │                            │
//   │ COOLANT  ← user label      │  (rendered at cfg.labelPosition when set)
//   └────────────────────────────┘
// Header and user label are mutually exclusive — same as studio.

#include "label_widget.h"
#include "diag/logger.h"
#include "ui/font_manager.h"
#include "ui/widget_label.h"
#include <lvgl.h>
#include <stdio.h>

namespace {

// Pick a value font size that fills `lineH` vertically and stays under 52% of
// `widgetW` horizontally — matches studio's `Math.min(valueLineH * 0.72, w * 0.52)`.
// Result is then snapped by FontManager to the nearest compiled-in Montserrat.
uint8_t pickValueFontSize(int16_t lineH, int16_t widgetW) {
    const int byHeight = (lineH * 72) / 100;
    const int byWidth  = (widgetW * 52) / 100;
    int s = byHeight < byWidth ? byHeight : byWidth;
    if (s < 12) s = 12;
    if (s > 48) s = 48;
    return static_cast<uint8_t>(s);
}

uint8_t pickUnitFontSize(int16_t lineH, int16_t widgetW) {
    const int byHeight = (lineH * 72) / 100;
    const int byWidth  = (widgetW * 28) / 100;
    int s = byHeight < byWidth ? byHeight : byWidth;
    if (s < 12) s = 12;
    if (s > 20) s = 20;
    return static_cast<uint8_t>(s);
}

} // namespace

lv_obj_t *LabelWidget::create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    lv_obj_t *cont = lv_obj_create(parent);
    if (!cont) {
        // LVGL pool is exhausted (LV_USE_LOG=0 silences LVGL's own warning).
        // Bail out instead of letting the next LVGL call deref NULL and panic.
        LOG_ERROR("WF", "lv_obj_create failed for '%s' — LVGL pool OOM", cfg.id);
        return nullptr;
    }
    lv_obj_set_pos(cont, cfg.layout.x, cfg.layout.y + yOffset);
    lv_obj_set_size(cont, cfg.layout.w, cfg.layout.h);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont, 0, LV_PART_MAIN);
    if (cfg.style.hasBorder) {
        lv_obj_set_style_border_width(cont, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(cont, lv_color_hex(cfg.style.borderColor.rgb), LV_PART_MAIN);
    } else {
        lv_obj_set_style_border_width(cont, 0, LV_PART_MAIN);
    }

    const bool hasUserLabel = cfg.label.label[0] != '\0';
    const bool hasSuffix = cfg.label.suffix[0] != '\0';

    // Auto signal header eats ~14 px from the top — only when no user label.
    const int16_t sigHeaderH = hasUserLabel ? 0 : 14;
    const int16_t availH = cfg.layout.h - sigHeaderH;
    // Suffix sits in its own line worth ~24 % of the remaining height (studio).
    const int16_t unitLineH = hasSuffix ? (availH * 24) / 100 : 0;
    const int16_t valueLineH = availH - unitLineH;

    const lv_font_t *valueFont =
        FontManager::get(pickValueFontSize(valueLineH, cfg.layout.w));
    const lv_font_t *unitFont =
        FontManager::get(pickUnitFontSize(unitLineH, cfg.layout.w));

    if (!hasUserLabel) {
        WidgetLabelOverlay::applySignalHeader(cont, cfg.signalId);
    }

    lv_obj_t *label = lv_label_create(cont);
    if (!label) {
        LOG_ERROR("WF", "lv_label_create failed for '%s' — LVGL pool OOM", cfg.id);
        lv_obj_del(cont);
        return nullptr;
    }
    lv_obj_set_style_text_color(label, lv_color_hex(cfg.style.textColor.rgb), 0);
    lv_obj_set_style_text_font(label, valueFont, 0);

    // Centre the value inside the value-line band: shift down by half the
    // signal-header reserve and up by half the suffix-line reserve.
    const int16_t valueYOffset = static_cast<int16_t>((sigHeaderH - unitLineH) / 2);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, valueYOffset);

    {
        char valBuf[16];
        snprintf(valBuf, sizeof(valBuf), "%.*f", static_cast<int>(cfg.label.decimalPlaces), 0.0f);
        char buf[32];
        snprintf(buf, sizeof(buf), "%s%s", cfg.label.prefix, valBuf);
        lv_label_set_text(label, buf);
    }

    if (hasSuffix) {
        lv_obj_t *unitLbl = lv_label_create(cont);
        if (unitLbl) {
            lv_label_set_text(unitLbl, cfg.label.suffix);
            lv_obj_set_style_text_color(unitLbl,
                                         lv_color_hex(WidgetLabelOverlay::kLabelDimRgb), 0);
            lv_obj_set_style_text_font(unitLbl, unitFont, 0);
            lv_obj_align(unitLbl, LV_ALIGN_BOTTOM_MID, 0, -2);
        }
    }

    if (hasUserLabel) {
        WidgetLabelOverlay::apply(cont, cfg.label.label, cfg.label.labelPosition,
                                   cfg.style.textColor.rgb);
    }

    // Update path only ever rewrites the value label — store its handle.
    lv_obj_set_user_data(cont, label);

    return cont;
}

void LabelWidget::update(lv_obj_t *obj, float value, bool valid, const CfgWidget &cfg) {
    if (!obj)
        return;
    lv_obj_t *label = static_cast<lv_obj_t *>(lv_obj_get_user_data(obj));
    if (!label)
        return;

    if (!valid && cfg.label.hideWhenInvalid) {
        lv_label_set_text(label, "");
        return;
    }

    // Invalid signals fall through with value=0 — same formatting as live
    // values so the dashboard always reads numerically.
    const float displayValue = valid ? value : 0.0f;

    char valBuf[16];
    snprintf(valBuf, sizeof(valBuf), "%.*f",
             static_cast<int>(cfg.label.decimalPlaces), displayValue);

    char buf[32];
    snprintf(buf, sizeof(buf), "%s%s", cfg.label.prefix, valBuf);
    lv_label_set_text(label, buf);
}
