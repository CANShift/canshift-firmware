#pragma once
// widget_label.h — Optional corner label drawn on every widget that supports it.
//
// Mirrors the studio preview's svgLabelAttrs() positioning: a small dim caps
// header pinned to one of the six corners of the widget container. The colour
// is a fixed neutral grey (#888888) — the previous half-luminance trick turned
// medium-saturated text colours (cyan, orange) into unreadable mud against
// the dark dashboard background.

#include <lvgl.h>
#include <string.h>
#include "config/config_types.h"
#include "ui/font_manager.h"

namespace WidgetLabelOverlay {

// Studio uses #888888 with letter-spacing 0.06em + 5–7 px font. We mirror that
// with Montserrat 12 (smallest compiled-in size that stays legible on the
// 320×240 panel) and a 1-px letter spacing for the same caps look.
constexpr uint32_t kLabelDimRgb = 0x888888;

// No-op when `text` is empty or null. The label is parented to `cont` and
// positioned according to `pos`. The `textColor` arg is kept for ABI stability
// across call sites but is no longer used — labels are always neutral grey so
// they never compete with coloured value text.
//
// The label is given an explicit width (parent width minus a small margin) +
// LV_LABEL_LONG_DOT so an oversized label is ellipsized rather than clipped.
inline void apply(lv_obj_t *cont, const char *text, CfgLabelPos pos, uint32_t textColor) {
    (void)textColor;
    if (!cont || !text || text[0] == '\0')
        return;

    lv_obj_t *lbl = lv_label_create(cont);
    lv_label_set_text(lbl, text);

    lv_obj_set_style_text_color(lbl, lv_color_hex(kLabelDimRgb), 0);
    lv_obj_set_style_text_font(lbl, FontManager::get(12), 0);
    lv_obj_set_style_text_letter_space(lbl, 1, 0);

    const lv_coord_t parentW = lv_obj_get_width(cont);
    if (parentW > 8) {
        lv_obj_set_width(lbl, parentW - 4);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    }

    // Slight inset from the widget edge so the caps text never visually
    // touches the corner — matches studio's `padding: 4px` on the SVG side.
    constexpr int16_t kEdgeInsetX = 4;
    constexpr int16_t kEdgeInsetY = 1;
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

// Curated short labels for known signals — fits the 80-px-wide cells used by
// the small numeric widgets where the auto-formatted "COOLANT TEMP C" would
// overflow. Returns nullptr when the signal isn't in the dictionary; callers
// then fall back to the auto-formatted version.
//
// Keep in sync with the studio dictionary in
// `canshift-studio/src/utils/signalLabels.ts`.
inline const char *displayLabelForSignal(const char *signalId) {
    if (!signalId || signalId[0] == '\0') return nullptr;
    if (strcmp(signalId, "rpm") == 0)               return "RPM";
    if (strcmp(signalId, "speed_kph") == 0)         return "SPEED";
    if (strcmp(signalId, "coolant_temp_c") == 0)    return "COOLANT";
    if (strcmp(signalId, "oil_temp_c") == 0)        return "OIL TEMP";
    if (strcmp(signalId, "oil_press_bar") == 0)     return "OIL PRESS";
    if (strcmp(signalId, "boost_bar") == 0)         return "BOOST";
    if (strcmp(signalId, "throttle_pos") == 0)      return "THROTTLE";
    if (strcmp(signalId, "gear") == 0)              return "GEAR";
    if (strcmp(signalId, "afr_1") == 0)             return "AFR";
    if (strcmp(signalId, "lambda_1") == 0)          return "LAMBDA";
    if (strcmp(signalId, "iat_c") == 0)             return "IAT";
    if (strcmp(signalId, "battery_volts") == 0)     return "BATT";
    if (strcmp(signalId, "flag_mil") == 0)          return "MIL";
    if (strcmp(signalId, "flag_anti_lag") == 0)     return "ALS";
    if (strcmp(signalId, "flag_launch_ctrl") == 0)  return "LAUNCH";
    if (strcmp(signalId, "flag_traction_cut") == 0) return "TC";
    if (strcmp(signalId, "flag_flat_shift") == 0)   return "FLAT SHIFT";
    return nullptr;
}

// Auto signal-name header — drawn at top-left in the same dim caps style when
// the widget has no user-configured `cfg.label`. Uses the curated dictionary
// when available, otherwise falls back to a simple uppercase / underscore-to-
// space transform of the signal id.
inline void applySignalHeader(lv_obj_t *cont, const char *signalId) {
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
