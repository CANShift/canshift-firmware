#pragma once

#include <lvgl.h>
#include <stdint.h>

#include "lgfx_panel.h"

namespace DisplayDriver {

void init();
void registerWithLVGL();
void setBacklight(uint8_t brightness);
void flushCallback(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *colorMap);

LGFX &getDisplay();

} // namespace DisplayDriver
