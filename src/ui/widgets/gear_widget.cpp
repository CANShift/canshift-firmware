// gear_widget.cpp — Large gear indicator widget
//
// Renders the current gear as an oversized centered number.
// Special values:
//   0 (or invalid)  → "N" (neutral)
//  -1 (negative)    → "R" (reverse)
//  1–8              → "1"–"8"
//
// Font is auto-selected from the largest that fits the widget height.

#include "gear_widget.h"
#include "diag/logger.h"

#include <lvgl.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

const lv_font_t *selectFont(int16_t height) {
    if (height >= 120)
        return &lv_font_montserrat_48;
    if (height >= 80)
        return &lv_font_montserrat_32;
    if (height >= 56)
        return &lv_font_montserrat_24;
    if (height >= 40)
        return &lv_font_montserrat_20;
    return &lv_font_montserrat_16;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

lv_obj_t *GearWidget::create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
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

    lv_obj_t *label = lv_label_create(cont);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(cfg.style.textColor.rgb), 0);
    lv_obj_set_style_text_font(label, selectFont(cfg.layout.h), 0);
    lv_label_set_text(label, "N");

    lv_obj_set_user_data(cont, label);
    return cont;
}

void GearWidget::update(lv_obj_t *obj, float value, bool valid, const CfgWidget &cfg) {
    if (!obj)
        return;
    lv_obj_t *label = static_cast<lv_obj_t *>(lv_obj_get_user_data(obj));
    if (!label)
        return;

    if (!valid || value == 0.0f) {
        lv_label_set_text(label, "N");
        lv_obj_set_style_text_color(label, lv_color_hex(cfg.style.textColor.rgb), 0);
        return;
    }

    int32_t gear = static_cast<int32_t>(value);
    if (gear < 0) {
        lv_label_set_text(label, "R");
        lv_obj_set_style_text_color(label, lv_color_hex(cfg.style.warningColor.rgb), 0);
        return;
    }

    char buf[4];
    snprintf(buf, sizeof(buf), "%d", gear);
    lv_label_set_text(label, buf);
    lv_obj_set_style_text_color(label, lv_color_hex(cfg.style.textColor.rgb), 0);
}
