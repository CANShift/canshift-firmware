#include "top_bar_rev_limit.h"
#include "top_bar_internal.h"

#include "config/config_loader.h"
#include "ui/font_manager.h"
#include "ui/rev_limit_flash.h"
#include "ui/widgets/widget_helpers.h"

#include <math.h>
#include <stdio.h>

using namespace TopBarInternal;

namespace {

lv_obj_t *s_field = nullptr;
bool s_overriding = false;
uint32_t s_lastColor = COLOR_UNSET;

bool defaultHiddenFor(TopBarItemKind kind) {
    return kind == TopBarItemKind::MODE_FLAG || kind == TopBarItemKind::SEPARATOR;
}

void takeRightGroup() {
    for (uint8_t i = 0; i < s_dynCount; ++i) {
        DynItem &d = s_dynItems[i];
        if (d.position != TopBarItemPos::RIGHT)
            continue;
        d.hidden = true;
        WidgetHelpers::setVisible(d.obj, false);
    }
}

void releaseRightGroup() {
    for (uint8_t i = 0; i < s_dynCount; ++i) {
        DynItem &d = s_dynItems[i];
        if (d.position != TopBarItemPos::RIGHT)
            continue;
        d.hidden = defaultHiddenFor(d.kind);
        d.lastText[0] = '\0';
        d.lastColor = COLOR_UNSET;
        WidgetHelpers::setVisible(d.obj, !d.hidden);
    }
}

void paintField() {
    const uint32_t color = labelColor();
    if (color == s_lastColor)
        return;
    lv_obj_set_style_text_color(s_field, lv_color_hex(color), 0);
    s_lastColor = color;
}

void onFieldDeleted(lv_event_t *) {
    s_field = nullptr;
    s_overriding = false;
    s_lastColor = COLOR_UNSET;
}

void writeLimitText() {
    char buf[DYN_TEXT_CAP];
    snprintf(buf, sizeof(buf), "LIMIT %ld",
             lroundf(ConfigLoader::getDashboardConfig().revLimitRpm));
    lv_label_set_text(s_field, buf);
}

} // namespace

void TopBarRevLimit::build(lv_obj_t *bar) {
    s_overriding = false;
    s_lastColor = COLOR_UNSET;
    s_field = lv_label_create(bar);
    if (!s_field)
        return;
    lv_obj_set_style_text_font(s_field, FontManager::units(), 0);
    lv_obj_set_style_text_letter_space(s_field, BAR_LETTER_SPACE_PX, 0);
    lv_obj_align(s_field, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_flag(s_field, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_field, onFieldDeleted, LV_EVENT_DELETE, nullptr);
    paintField();
    writeLimitText();
}

void TopBarRevLimit::clearOverride() {
    s_overriding = false;
    if (s_field)
        lv_obj_add_flag(s_field, LV_OBJ_FLAG_HIDDEN);
}

bool TopBarRevLimit::apply() {
    const bool wanted = s_field != nullptr && RevLimitFlash::isEngaged();
    if (wanted != s_overriding) {
        s_overriding = wanted;
        WidgetHelpers::setVisible(s_field, wanted);
        if (wanted) {
            takeRightGroup();
            writeLimitText();
        } else {
            releaseRightGroup();
        }
    }
    if (s_overriding)
        paintField();
    return s_overriding;
}
