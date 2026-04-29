// warning_widget.cpp

#include "warning_widget.h"
#include <lvgl.h>

lv_obj_t *WarningWidget::create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    lv_obj_t *led = lv_led_create(parent);
    int32_t size = cfg.layout.w < cfg.layout.h ? cfg.layout.w : cfg.layout.h;
    if (size < 10)
        size = 10;
    lv_obj_set_size(led, size, size);
    lv_obj_set_pos(led, cfg.layout.x, cfg.layout.y + yOffset);
    lv_led_set_color(led, lv_color_hex(cfg.style.criticalColor.rgb));
    lv_led_off(led);
    return led;
}

void WarningWidget::update(lv_obj_t *obj, float value, bool valid, const CfgWidget &cfg) {
    if (!obj)
        return;

    bool active;
    if (!valid) {
        active = false;
    } else if (cfg.warning.invertLogic) {
        active = value < cfg.warning.threshold;
    } else {
        active = value >= cfg.warning.threshold;
    }

    if (active) {
        lv_led_on(obj);
    } else {
        lv_led_off(obj);
    }
}
