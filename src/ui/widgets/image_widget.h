// image_widget.h — Static SPIFFS image widget (BMP via LVGL FS driver)
#pragma once

#include "config/config_types.h"
#include <lvgl.h>

class ImageWidget {
public:
    static lv_obj_t *create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset);
    // Image widgets are static — no update needed
};
