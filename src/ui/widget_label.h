#pragma once
// widget_label.h — Optional corner label drawn on every widget that supports it.
//
// Mirrors the studio preview's svgLabelAttrs() positioning: a small dim text
// pinned to one of the six corners of the widget container. The text uses
// half-luminance of the widget's textColor so it stays out of the way of the
// primary content.

#include <lvgl.h>
#include "config/config_types.h"

namespace WidgetLabelOverlay {

// No-op when `text` is empty or null. The label is parented to `cont`,
// positioned according to `pos`, and tinted with a dimmed `textColor`.
inline void apply(lv_obj_t *cont, const char *text, CfgLabelPos pos, uint32_t textColor) {
    if (!cont || !text || text[0] == '\0')
        return;

    lv_obj_t *lbl = lv_label_create(cont);
    lv_label_set_text(lbl, text);

    // Half-luminance tone so the label is visible but never competes with
    // the primary content. Same trick used by studio (textColor + 77 alpha).
    const uint32_t dimRgb = (textColor >> 1) & 0x7F7F7F;
    lv_obj_set_style_text_color(lbl, lv_color_hex(dimRgb), 0);

    switch (pos) {
        case CfgLabelPos::TOP_LEFT:      lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 2, 1); break;
        case CfgLabelPos::TOP_CENTER:    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 1); break;
        case CfgLabelPos::TOP_RIGHT:     lv_obj_align(lbl, LV_ALIGN_TOP_RIGHT, -2, 1); break;
        case CfgLabelPos::BOTTOM_LEFT:   lv_obj_align(lbl, LV_ALIGN_BOTTOM_LEFT, 2, -1); break;
        case CfgLabelPos::BOTTOM_CENTER: lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -1); break;
        case CfgLabelPos::BOTTOM_RIGHT:  lv_obj_align(lbl, LV_ALIGN_BOTTOM_RIGHT, -2, -1); break;
    }
}

} // namespace WidgetLabelOverlay
