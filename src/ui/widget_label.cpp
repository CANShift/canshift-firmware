#include "widget_label.h"

#include "ui/theme_manager.h"

#include "ui/font_manager.h"
#include "ui/signal_presentation.h"

#include <lvgl.h>
#include <stddef.h>

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
constexpr lv_coord_t kWidgetRightInsetPx = 8;

lv_obj_t *drawHeader(lv_obj_t *cont, const char *text, HeaderPos pos) {
    lv_obj_t *lbl = lv_label_create(cont);
    lv_label_set_text(lbl, text);

    lv_obj_set_style_text_color(lbl, lv_color_hex(ThemeManager::dimColor()), 0);
    lv_obj_set_style_text_font(lbl, FontManager::label(10), 0);
    lv_obj_set_style_text_letter_space(lbl, kKickerTrackingPx, 0);

    const lv_coord_t parentW = lv_obj_get_width(cont);
    if (parentW > kWidgetRightInsetPx) {
        lv_obj_set_width(lbl, static_cast<lv_coord_t>(parentW - kWidgetRightInsetPx));
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
    }

    alignHeader(lbl, pos);
    return lbl;
}

} // namespace

lv_obj_t *applySignalHeader(lv_obj_t *cont, const char *signalId, HeaderPos pos) {
    if (!cont || !signalId || signalId[0] == '\0')
        return nullptr;

    const char *curated = SignalPresentation::kickerForSignal(signalId);
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
