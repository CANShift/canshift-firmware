// image_widget.h — Static SPIFFS image widget (BMP via LVGL FS driver)
#pragma once

#include "config/config_types.h"
#include <lvgl.h>

namespace ImageWidget {

// Image widgets are static — no update needed.
lv_obj_t *create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset);

} // namespace ImageWidget
