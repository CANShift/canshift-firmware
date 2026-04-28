#pragma once
// button_widget.h — Page navigation / action button widget

#include <lvgl.h>
#include "config/config_types.h"

namespace ButtonWidget {
    lv_obj_t* create(lv_obj_t* parent, const CfgWidget& cfg, int16_t yOffset);
    // Buttons are event-driven — no update() needed
} // namespace ButtonWidget
