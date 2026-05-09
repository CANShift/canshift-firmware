// gear_widget.cpp — Large gear indicator widget
//
// Renders the current gear as an oversized centered number.
// Special values:
//   0 (or invalid)  → "N" (neutral)
//  -1 (negative)    → "R" (reverse)
//  1–8              → "1"–"8"
//
// Font is auto-selected from the largest that fits the widget height.

#include "gear_widget.h"
#include "ui/font_manager.h"
#include "ui/theme_manager.h"
#include "ui/widget_label.h"
#include "ui/widget_styles.h"
#include "diag/logger.h"

#include <lvgl.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

// Tag promoted from a bare lv_obj_t* pointer so we can cache the last text
// colour and skip the redundant lv_obj_set_style_text_color writes per tick.
struct GearTag {
    lv_obj_t *label;
    uint32_t lastColorRgb; // 0xFFFFFFFFu sentinel forces the first paint.
};

const lv_font_t *selectFont(int16_t height) {
    if (height >= 120)
        return FontManager::primary(48);
    if (height >= 80)
        return FontManager::primary(32);
    if (height >= 56)
        return FontManager::secondary(24);
    if (height >= 40)
        return FontManager::secondary(20);
    return FontManager::label(16);
}

// Skip the lv_label_set_text reallocation when the formatted gear is already
// shown (issue #236). Gear values change at human cadence, so almost every
// tick the label is unchanged.
void setLabelIfChanged(lv_obj_t *label, const char *text) {
    const char *current = lv_label_get_text(label);
    if (current == nullptr || strcmp(current, text) != 0) {
        lv_label_set_text(label, text);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

lv_obj_t *GearWidget::create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_pos(cont, cfg.layout.x, cfg.layout.y + yOffset);
    lv_obj_set_size(cont, cfg.layout.w, cfg.layout.h);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    WidgetStyles::applyContainerBase(cont, cfg.style.hasBorder, cfg.style.borderColor.rgb);

    const uint32_t textRgb = ThemeManager::getEffectiveTextColor();
    lv_obj_t *label = lv_label_create(cont);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(textRgb), 0);
    lv_obj_set_style_text_font(label, selectFont(cfg.layout.h), 0);
    lv_label_set_text(label, "N");

    auto *tag = new GearTag{};
    tag->label = label;
    tag->lastColorRgb = textRgb; // First paint above already pushed textRgb.
    lv_obj_set_user_data(cont, tag);
    lv_obj_add_event_cb(
        cont,
        [](lv_event_t *e) {
            auto *t = static_cast<GearTag *>(lv_event_get_user_data(e));
            delete t;
        },
        LV_EVENT_DELETE, tag);

    // Optional widget label (gear shares CfgLabelParams in the union).
    WidgetLabelOverlay::apply(cont, cfg.label.label, cfg.label.labelPosition, textRgb);

    return cont;
}

void GearWidget::update(lv_obj_t *obj, float value, bool valid, const CfgWidget &cfg) {
    if (!obj)
        return;
    auto *tag = static_cast<GearTag *>(lv_obj_get_user_data(obj));
    if (!tag || !tag->label)
        return;
    lv_obj_t *label = tag->label;

    const uint32_t textRgb = ThemeManager::getEffectiveTextColor();

    if (!valid || value == 0.0f) {
        setLabelIfChanged(label, "N");
        WidgetStyles::setTextColorIfChanged(label, tag->lastColorRgb, textRgb);
        return;
    }

    int32_t gear = static_cast<int32_t>(value);
    if (gear < 0) {
        setLabelIfChanged(label, "R");
        WidgetStyles::setTextColorIfChanged(label, tag->lastColorRgb,
                                            cfg.style.warningColor.rgb);
        return;
    }

    char buf[4];
    snprintf(buf, sizeof(buf), "%d", gear);
    setLabelIfChanged(label, buf);
    WidgetStyles::setTextColorIfChanged(label, tag->lastColorRgb, textRgb);
}
