#include "widget_styles.h"

namespace WidgetStyles {

void applyContainerBase(lv_obj_t *cont, bool hasBorder, uint32_t borderRgb) {
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont, 0, LV_PART_MAIN);
    if (hasBorder) {
        lv_obj_set_style_border_width(cont, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(cont, lv_color_hex(borderRgb), LV_PART_MAIN);
    } else {
        lv_obj_set_style_border_width(cont, 0, LV_PART_MAIN);
    }
}

void applyContainerBaseNoBorder(lv_obj_t *cont) {
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont, 0, LV_PART_MAIN);
}

void disableArcKnob(lv_obj_t *arc) {
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
}

bool setTextColorIfChanged(lv_obj_t *label, uint32_t &cachedRgb, uint32_t targetRgb) {
    if (cachedRgb == targetRgb)
        return false;
    lv_obj_set_style_text_color(label, lv_color_hex(targetRgb), 0);
    cachedRgb = targetRgb;
    return true;
}

bool setBgColorIfChanged(lv_obj_t *obj, uint32_t &cachedRgb, uint32_t targetRgb) {
    if (cachedRgb == targetRgb)
        return false;
    lv_obj_set_style_bg_color(obj, lv_color_hex(targetRgb), LV_PART_MAIN);
    cachedRgb = targetRgb;
    return true;
}

bool setArcColorIfChanged(lv_obj_t *arc, uint32_t &cachedRgb, uint32_t targetRgb, lv_part_t part) {
    if (cachedRgb == targetRgb)
        return false;
    lv_obj_set_style_arc_color(arc, lv_color_hex(targetRgb), part);
    cachedRgb = targetRgb;
    return true;
}

}
