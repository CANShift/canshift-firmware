#pragma once
// display_driver.h — ILI9341 display HAL (LovyanGFX backend)
//
// Wraps the LGFX class (see include/lgfx_panel.h) and provides the LVGL flush
// callback. getDisplay() exposes the single shared LGFX instance so that
// touch_driver can use the same panel for getTouch()/calibrateTouch().

#include <lvgl.h>
#include <stdint.h>

#if !defined(APP_SIMULATION_MODE) || !APP_SIMULATION_MODE
    #include "lgfx_panel.h"
#endif

namespace DisplayDriver {

void init();
void registerWithLVGL();
void setBacklight(uint8_t brightness);
void flushCallback(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *colorMap);

#if !defined(APP_SIMULATION_MODE) || !APP_SIMULATION_MODE
LGFX &getDisplay();
#endif

} // namespace DisplayDriver
