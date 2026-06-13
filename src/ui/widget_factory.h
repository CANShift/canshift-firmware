#pragma once

#include <lvgl.h>
#include "config/config_types.h"

namespace WidgetFactory {

lv_obj_t *create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset = 0);

void updateAll(lv_obj_t *parent);

void clearAll(lv_obj_t *parent);

void reapplyTheme(lv_obj_t *parent);

} // namespace WidgetFactory
