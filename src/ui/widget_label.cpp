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

constexpr int16_t kKickerTrackingPx = 2;

lv_obj_t *drawHeader(lv_obj_t *cont, const char *text, HeaderPos pos) {
    lv_obj_t *lbl = lv_label_create(cont);
    lv_label_set_text(lbl, text);

    lv_obj_set_style_text_color(lbl, lv_color_hex(kLabelDimRgb), 0);
    lv_obj_set_style_text_font(lbl, FontManager::label(10), 0);
    lv_obj_set_style_text_letter_space(lbl, kKickerTrackingPx, 0);

    const lv_coord_t parentW = lv_obj_get_width(cont);
    if (parentW > 8) {
        lv_obj_set_width(lbl, parentW - 4);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    }

    alignHeader(lbl, pos);
    return lbl;
}

} // namespace

struct SignalPresentation {
    const char *signalId;
    const char *kicker;
    const char *unit;
};

constexpr SignalPresentation kSignalPresentation[] = {
    {"rpm", "RPM", "rpm"},
    {"speed_kph", "SPEED", "km/h"},
    {"coolant_temp_c", "WATER", "\u00b0C"},
    {"oil_temp_c", "OIL", "\u00b0C"},
    {"oil_press_bar", "OIL", "bar"},
    {"fuel_press_bar", "FUEL", "bar"},
    {"fuel_level_pct", "FUEL", "%"},
    {"map_kpa", "MAP", "kPa"},
    {"boost_bar", "BOOST", "bar"},
    {"throttle_pos", "TPS", "%"},
    {"gear", "GEAR", ""},
    {"afr_1", "AFR", ""},
    {"lambda_1", "LAMBDA", "\u03bb"},
    {"iat_c", "IAT", "\u00b0C"},
    {"egt_c", "EGT", "\u00b0C"},
    {"gearbox_temp_c", "GEARBOX", "\u00b0C"},
    {"diff_temp_c", "DIFF", "\u00b0C"},
    {"knock_count", "KNOCK", ""},
    {"clutch_state", "CLUTCH", ""},
    {"odo_km", "ODO", "km"},
    {"trip_km", "TRIP", "km"},
    {"battery_volts", "BATT", "V"},
    {"flag_mil", "MIL", ""},
    {"flag_anti_lag", "ALS", ""},
    {"flag_launch_ctrl", "LAUNCH", ""},
    {"flag_traction_cut", "TC", ""},
    {"flag_flat_shift", "FLAT SHIFT", ""},
};

const SignalPresentation *findPresentation(const char *signalId) {
    if (!signalId || signalId[0] == '\0')
        return nullptr;
    for (const SignalPresentation &entry : kSignalPresentation) {
        if (strcmp(signalId, entry.signalId) == 0)
            return &entry;
    }
    return nullptr;
}

const char *displayLabelForSignal(const char *signalId) {
    const SignalPresentation *entry = findPresentation(signalId);
    return entry != nullptr ? entry->kicker : nullptr;
}

const char *displayUnitForSignal(const char *signalId) {
    const SignalPresentation *entry = findPresentation(signalId);
    return entry != nullptr ? entry->unit : "";
}

lv_obj_t *applySignalHeader(lv_obj_t *cont, const char *signalId, HeaderPos pos) {
    if (!cont || !signalId || signalId[0] == '\0')
        return nullptr;

    const char *curated = displayLabelForSignal(signalId);
    if (curated != nullptr)
        return drawHeader(cont, curated, pos);

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

    return drawHeader(cont, buf, pos);
}

} // namespace WidgetLabelOverlay
