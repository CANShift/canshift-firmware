#pragma once

#include "config/config_types.h"
#include <lvgl.h>

namespace CruiseControlWidget {

void build(lv_obj_t *screen, const CfgPage &cfg, int16_t contentY);

} // namespace CruiseControlWidget
