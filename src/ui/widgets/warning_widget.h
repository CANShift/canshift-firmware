#pragma once

#include <lvgl.h>
#include "config/config_types.h"

namespace WarningWidget {
lv_obj_t *create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset);
void reapplyTheme(lv_obj_t *obj, const CfgWidget &cfg);
void update(lv_obj_t *obj, float value, bool valid, const CfgWidget &cfg);
} // namespace WarningWidget
