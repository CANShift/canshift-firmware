// button_widget.cpp — Page navigation button

#include "button_widget.h"
#include "ui/page_manager.h"
#include <lvgl.h>
#include <string.h>

namespace {

static void btnClickHandler(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;

    const CfgButtonParams *params = static_cast<const CfgButtonParams *>(lv_event_get_user_data(e));
    if (!params)
        return;

    if (strlen(params->targetPageId) > 0) {
        PageManager::navigateTo(params->targetPageId);
    }
}

} // namespace

lv_obj_t *ButtonWidget::create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_pos(btn, cfg.layout.x, cfg.layout.y + yOffset);
    lv_obj_set_size(btn, cfg.layout.w, cfg.layout.h);

    lv_obj_set_style_bg_color(btn, lv_color_hex(cfg.style.primaryColor.rgb), LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 6, LV_PART_MAIN);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, cfg.button.label);
    lv_obj_set_style_text_color(label, lv_color_hex(cfg.style.textColor.rgb), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

    // Store a copy of the button params for the event handler
    // NOTE: cfg is kept alive by WidgetFactory's registry — safe to take pointer
    lv_obj_add_event_cb(btn, btnClickHandler, LV_EVENT_CLICKED,
                        const_cast<CfgButtonParams *>(&cfg.button));

    return btn;
}
