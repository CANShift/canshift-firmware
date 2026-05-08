// button_widget.cpp — Tap-action button (page nav, ECU map switch, raw CAN, …)

#include "button_widget.h"
#include "ui/page_manager.h"
#include "diag/logger.h"
#include <lvgl.h>
#include <string.h>

namespace {

void dispatchAction(const CfgButtonAction &a) {
    switch (a.type) {
        case CfgButtonActionType::NAV_PAGE:
            if (a.pageId[0] != '\0')
                PageManager::navigateTo(a.pageId);
            break;
        case CfgButtonActionType::MAP_SWITCH:
            // TODO(#XXX): wire ECU map_switch dispatch — tracked as follow-up.
            LOG_INFO("BTN", "map_switch action (mapIndex=%u) not yet implemented",
                     static_cast<unsigned>(a.mapIndex));
            break;
        case CfgButtonActionType::CAN_RAW:
            // TODO(#XXX): wire raw CAN frame send — tracked as follow-up.
            LOG_INFO("BTN", "can_raw action (frameId=0x%lX) not yet implemented",
                     static_cast<unsigned long>(a.canFrameId));
            break;
        case CfgButtonActionType::UNKNOWN:
        default:
            break;
    }
}

void btnClickHandler(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;

    const CfgButtonParams *params = static_cast<const CfgButtonParams *>(lv_event_get_user_data(e));
    if (!params)
        return;

    for (uint8_t i = 0; i < params->actionsCount; ++i) {
        dispatchAction(params->actions[i]);
    }
}

} // namespace

lv_obj_t *ButtonWidget::create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_pos(btn, cfg.layout.x, cfg.layout.y + yOffset);
    lv_obj_set_size(btn, cfg.layout.w, cfg.layout.h);

    // Idle / pressed background — honor `colors{normal,active}` when present,
    // otherwise fall back to the legacy `widget.style.primaryColor`.
    const uint32_t bgNormal =
        cfg.button.hasColors ? cfg.button.colorNormal.rgb : cfg.style.primaryColor.rgb;
    const uint32_t bgActive =
        cfg.button.hasColors ? cfg.button.colorActive.rgb : cfg.style.primaryColor.rgb;
    lv_obj_set_style_bg_color(btn, lv_color_hex(bgNormal), LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bgActive), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 0, LV_PART_MAIN);

    if (cfg.button.showLabel && cfg.button.label[0] != '\0') {
        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, cfg.button.label);
        lv_obj_set_style_text_color(label, lv_color_hex(cfg.style.textColor.rgb), 0);
        lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    }

    // Store a pointer to the button params for the event handler.
    // NOTE: cfg is kept alive by WidgetFactory's registry — safe to take pointer.
    lv_obj_add_event_cb(btn, btnClickHandler, LV_EVENT_CLICKED,
                        const_cast<CfgButtonParams *>(&cfg.button));

    return btn;
}
