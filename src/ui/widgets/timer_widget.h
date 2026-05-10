// timer_widget.h — Lap/session timer widget
#pragma once

#include "config/config_types.h"
#include <lvgl.h>

namespace TimerWidget {

lv_obj_t *create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset);
void update(lv_obj_t *obj, float value, bool valid, const CfgWidget &cfg);

} // namespace TimerWidget
