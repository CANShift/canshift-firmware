#pragma once
// button_widget.h — Page navigation / action button widget

#include <lvgl.h>
#include "config/config_types.h"

namespace ButtonWidget {
lv_obj_t *create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset);
// Updates the active-map badge on map_switch buttons. Call each render tick.
void update(lv_obj_t *btn);
} // namespace ButtonWidget
