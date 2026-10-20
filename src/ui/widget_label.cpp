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

char kickerChar(char c) {
    if (c == '_')
        return ' ';
    if (c >= 'a' && c <= 'z')
        return static_cast<char>(c - 'a' + 'A');
    return c;
}

}

void formatKicker(const char *raw, char *out, size_t outLen) {
    if (!out || outLen == 0)
        return;
    size_t i = 0;
    for (; raw && i < outLen - 1 && raw[i] != '\0'; ++i)
        out[i] = kickerChar(raw[i]);
    out[i] = '\0';
}

void signalKicker(const char *signalId, char *out, size_t outLen) {
    const char *curated = SignalPresentation::kickerForSignal(signalId);
    if (curated == nullptr) {
        SignalPresentation::fallbackKicker(signalId, out, outLen);
        return;
    }
    formatKicker(curated, out, outLen);
}

namespace {

bool isEmpty(const char *text) {
    return !text || text[0] == '\0';
}

bool composeKicker(const char *kicker, const char *signalId, char *out, size_t outLen) {
    if (!isEmpty(kicker)) {
        formatKicker(kicker, out, outLen);
        return true;
    }
    if (isEmpty(signalId))
        return false;
    signalKicker(signalId, out, outLen);
    return true;
}

}

lv_obj_t *applySignalHeader(lv_obj_t *cont, const char *signalId, HeaderPos pos) {
    return applyWidgetHeader(cont, nullptr, signalId, pos);
}

lv_obj_t *applyWidgetHeader(lv_obj_t *cont, const char *kicker, const char *signalId,
                            HeaderPos pos) {
    char buf[kKickerBufLen];
    if (!cont || !composeKicker(kicker, signalId, buf, sizeof(buf)))
        return nullptr;
    return drawHeader(cont, buf, pos);
}

}
