#pragma once

#include <lvgl.h>
#include "config/config_types.h"

namespace ButtonWidget {
lv_obj_t *create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset);

void update(lv_obj_t *btn);
} // namespace ButtonWidget
