// label_widget.cpp — Text value label widget

#include "label_widget.h"
#include <lvgl.h>
#include <stdio.h>

lv_obj_t *LabelWidget::create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_pos(cont, cfg.layout.x, cfg.layout.y + yOffset);
    lv_obj_set_size(cont, cfg.layout.w, cfg.layout.h);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont, 0, LV_PART_MAIN);

    lv_obj_t *label = lv_label_create(cont);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(cfg.style.textColor.rgb), 0);

    // Font selection based on configured size
    const lv_font_t *font = &lv_font_montserrat_16;
    if (cfg.style.fontSize >= 48)
        font = &lv_font_montserrat_48;
    else if (cfg.style.fontSize >= 32)
        font = &lv_font_montserrat_32;
    else if (cfg.style.fontSize >= 24)
        font = &lv_font_montserrat_24;
    else if (cfg.style.fontSize >= 20)
        font = &lv_font_montserrat_20;
    else if (cfg.style.fontSize >= 14)
        font = &lv_font_montserrat_14;
    else
        font = &lv_font_montserrat_12;

    lv_obj_set_style_text_font(label, font, 0);
    lv_label_set_text(label, "---");

    // Store label pointer in user data
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
    if (!valid) {
        lv_label_set_text(label, "---");
        return;
    }

    char valBuf[16];
    if (cfg.label.decimalPlaces == 0) {
        snprintf(valBuf, sizeof(valBuf), "%.0f", value);
    } else {
        snprintf(valBuf, sizeof(valBuf), "%.*f", (int)cfg.label.decimalPlaces, value);
    }

    char buf[48];
    snprintf(buf, sizeof(buf), "%s%s%s", cfg.label.prefix, valBuf, cfg.label.suffix);
    lv_label_set_text(label, buf);
}
