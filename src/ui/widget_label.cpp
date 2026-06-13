#include "widget_label.h"

#include "ui/font_manager.h"

#include <lvgl.h>
#include <string.h>

namespace WidgetLabelOverlay {

namespace {

constexpr int16_t kEdgeInsetX = 4;
constexpr int16_t kEdgeInsetY = 4;

void alignHeader(lv_obj_t *lbl, HeaderPos pos) {
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_LEFT, 0);
    switch (pos) {
        case HeaderPos::TOP_LEFT:
            lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, kEdgeInsetX, kEdgeInsetY);
            break;
        case HeaderPos::BOTTOM_LEFT:
            lv_obj_align(lbl, LV_ALIGN_BOTTOM_LEFT, kEdgeInsetX, -kEdgeInsetY);
            break;
    }
}

void drawHeader(lv_obj_t *cont, const char *text, HeaderPos pos) {
    lv_obj_t *lbl = lv_label_create(cont);
    lv_label_set_text(lbl, text);

    lv_obj_set_style_text_color(lbl, lv_color_hex(kLabelDimRgb), 0);
    lv_obj_set_style_text_font(lbl, FontManager::label(10), 0);
    lv_obj_set_style_text_letter_space(lbl, 1, 0);

    const lv_coord_t parentW = lv_obj_get_width(cont);
    if (parentW > 8) {
        lv_obj_set_width(lbl, parentW - 4);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    }

    alignHeader(lbl, pos);
}

} // namespace

// Mirrors the Studio dictionary in canshift-studio-web/src/utils/signalLabels.ts.
const char *displayLabelForSignal(const char *signalId) {
    if (!signalId || signalId[0] == '\0')
        return nullptr;
    if (strcmp(signalId, "rpm") == 0)
        return "RPM";
    if (strcmp(signalId, "speed_kph") == 0)
        return "SPEED";
    if (strcmp(signalId, "coolant_temp_c") == 0)
        return "COOLANT";
    if (strcmp(signalId, "oil_temp_c") == 0)
        return "OIL";
    if (strcmp(signalId, "oil_press_bar") == 0)
        return "OIL";
    if (strcmp(signalId, "fuel_press_bar") == 0)
        return "FUEL";
    if (strcmp(signalId, "map_kpa") == 0)
        return "MAP";
    if (strcmp(signalId, "boost_bar") == 0)
        return "BOOST";
    if (strcmp(signalId, "throttle_pos") == 0)
        return "TPS";
    if (strcmp(signalId, "gear") == 0)
        return "GEAR";
    if (strcmp(signalId, "afr_1") == 0)
        return "AFR";
    if (strcmp(signalId, "lambda_1") == 0)
        return "LAMBDA";
    if (strcmp(signalId, "iat_c") == 0)
        return "IAT";
    if (strcmp(signalId, "battery_volts") == 0)
        return "BATT";
    if (strcmp(signalId, "flag_mil") == 0)
        return "MIL";
    if (strcmp(signalId, "flag_anti_lag") == 0)
        return "ALS";
    if (strcmp(signalId, "flag_launch_ctrl") == 0)
        return "LAUNCH";
    if (strcmp(signalId, "flag_traction_cut") == 0)
        return "TC";
    if (strcmp(signalId, "flag_flat_shift") == 0)
        return "FLAT SHIFT";
    return nullptr;
}

void applySignalHeader(lv_obj_t *cont, const char *signalId, HeaderPos pos) {
    if (!cont || !signalId || signalId[0] == '\0')
        return;

    const char *curated = displayLabelForSignal(signalId);
    if (curated != nullptr) {
        drawHeader(cont, curated, pos);
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

    drawHeader(cont, buf, pos);
}

} // namespace WidgetLabelOverlay
