// widget_label.cpp — Implementation of the dim caps corner label overlay.
//
// See widget_label.h for the API contract. Bodies live here so the header
// can stay slim (no <lvgl.h> include) and the heavy LVGL drawing path
// touches LVGL only when actually used.

#include "widget_label.h"

#include "ui/font_manager.h"

#include <lvgl.h>
#include <string.h>

namespace WidgetLabelOverlay {

namespace {

constexpr int16_t kEdgeInsetX = 4;
constexpr int16_t kEdgeInsetY = 1;

void alignLabel(lv_obj_t *lbl, CfgLabelPos pos) {
    switch (pos) {
        case CfgLabelPos::TOP_LEFT:
            lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_LEFT, 0);
            lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, kEdgeInsetX, kEdgeInsetY);
            break;
        case CfgLabelPos::TOP_CENTER:
            lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, kEdgeInsetY);
            break;
        case CfgLabelPos::TOP_RIGHT:
            lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_align(lbl, LV_ALIGN_TOP_RIGHT, -kEdgeInsetX, kEdgeInsetY);
            break;
        case CfgLabelPos::BOTTOM_LEFT:
            lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_LEFT, 0);
            lv_obj_align(lbl, LV_ALIGN_BOTTOM_LEFT, kEdgeInsetX, -kEdgeInsetY);
            break;
        case CfgLabelPos::BOTTOM_CENTER:
            lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -kEdgeInsetY);
            break;
        case CfgLabelPos::BOTTOM_RIGHT:
            lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_align(lbl, LV_ALIGN_BOTTOM_RIGHT, -kEdgeInsetX, -kEdgeInsetY);
            break;
    }
}

} // namespace

void apply(lv_obj_t *cont, const char *text, CfgLabelPos pos, uint32_t textColor) {
    (void)textColor;
    if (!cont || !text || text[0] == '\0')
        return;

    lv_obj_t *lbl = lv_label_create(cont);
    lv_label_set_text(lbl, text);

    lv_obj_set_style_text_color(lbl, lv_color_hex(kLabelDimRgb), 0);
    lv_obj_set_style_text_font(lbl, FontManager::label(12), 0);
    lv_obj_set_style_text_letter_space(lbl, 1, 0);

    const lv_coord_t parentW = lv_obj_get_width(cont);
    if (parentW > 8) {
        lv_obj_set_width(lbl, parentW - 4);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    }

    alignLabel(lbl, pos);
}

const char *displayLabelForSignal(const char *signalId) {
    if (!signalId || signalId[0] == '\0')
        return nullptr;
    if (strcmp(signalId, "rpm") == 0) return "RPM";
    if (strcmp(signalId, "speed_kph") == 0) return "SPEED";
    if (strcmp(signalId, "coolant_temp_c") == 0) return "COOLANT";
    if (strcmp(signalId, "oil_temp_c") == 0) return "OIL TEMP";
    if (strcmp(signalId, "oil_press_bar") == 0) return "OIL PRESS";
    if (strcmp(signalId, "boost_bar") == 0) return "BOOST";
    if (strcmp(signalId, "throttle_pos") == 0) return "THROTTLE";
    if (strcmp(signalId, "gear") == 0) return "GEAR";
    if (strcmp(signalId, "afr_1") == 0) return "AFR";
    if (strcmp(signalId, "lambda_1") == 0) return "LAMBDA";
    if (strcmp(signalId, "iat_c") == 0) return "IAT";
    if (strcmp(signalId, "battery_volts") == 0) return "BATT";
    if (strcmp(signalId, "flag_mil") == 0) return "MIL";
    if (strcmp(signalId, "flag_anti_lag") == 0) return "ALS";
    if (strcmp(signalId, "flag_launch_ctrl") == 0) return "LAUNCH";
    if (strcmp(signalId, "flag_traction_cut") == 0) return "TC";
    if (strcmp(signalId, "flag_flat_shift") == 0) return "FLAT SHIFT";
    return nullptr;
}

void applySignalHeader(lv_obj_t *cont, const char *signalId) {
    if (!cont || !signalId || signalId[0] == '\0')
        return;

    const char *curated = displayLabelForSignal(signalId);
    if (curated != nullptr) {
        apply(cont, curated, CfgLabelPos::TOP_LEFT, 0);
        return;
    }

    char buf[32];
    size_t i = 0;
    for (; i < sizeof(buf) - 1 && signalId[i] != '\0'; ++i) {
        char c = signalId[i];
        if (c == '_')
            buf[i] = ' ';
        else if (c >= 'a' && c <= 'z')
            buf[i] = static_cast<char>(c - 'a' + 'A');
        else
            buf[i] = c;
    }
    buf[i] = '\0';

    apply(cont, buf, CfgLabelPos::TOP_LEFT, 0);
}

} // namespace WidgetLabelOverlay
