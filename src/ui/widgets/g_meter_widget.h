#pragma once

#include "config/config_types.h"
#include <lvgl.h>

namespace GMeterWidget {

lv_obj_t *create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset);
void update(lv_obj_t *obj);

}
