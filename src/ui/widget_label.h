#pragma once
// widget_label.h — Optional corner label drawn on every widget that supports it.
//
// Mirrors the studio preview's svgLabelAttrs() positioning: a small dim caps
// header pinned to one of the six corners of the widget container. The colour
// is a fixed neutral grey (#888888) — the previous half-luminance trick turned
// medium-saturated text colours (cyan, orange) into unreadable mud against
// the dark dashboard background.

#include <lvgl.h>
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

    switch (pos) {
        case CfgLabelPos::TOP_LEFT:
            lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_LEFT, 0);
            lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 2, 1);
            break;
        case CfgLabelPos::TOP_CENTER:
            lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 1);
            break;
        case CfgLabelPos::TOP_RIGHT:
            lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_align(lbl, LV_ALIGN_TOP_RIGHT, -2, 1);
            break;
        case CfgLabelPos::BOTTOM_LEFT:
            lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_LEFT, 0);
            lv_obj_align(lbl, LV_ALIGN_BOTTOM_LEFT, 2, -1);
            break;
        case CfgLabelPos::BOTTOM_CENTER:
            lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -1);
            break;
        case CfgLabelPos::BOTTOM_RIGHT:
            lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_align(lbl, LV_ALIGN_BOTTOM_RIGHT, -2, -1);
            break;
    }
}

// Auto signal-name header — drawn at top-left in the same dim caps style when
// the widget has no user-configured `cfg.label`. Mirrors studio's signalLabel
// behaviour (`coolant_temp_c` → `COOLANT TEMP C`).
inline void applySignalHeader(lv_obj_t *cont, const char *signalId) {
    if (!cont || !signalId || signalId[0] == '\0')
        return;

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
