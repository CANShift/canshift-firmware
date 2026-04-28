#pragma once
// touch_driver.h — XPT2046 resistive touch HAL
//
// Wraps TFT_eSPI's built-in XPT2046 support and registers an LVGL input device.
// Calibration values are in board_config.h.
//
// TODO: Run touch calibration on the real board and update TOUCH_CAL_* values.

#include <lvgl.h>

namespace TouchDriver {

    /**
     * Initialize the XPT2046 touch controller.
     * Must be called after DisplayDriver::init().
     */
    void init();

    /**
     * Poll touch input and feed into LVGL.
     * Call this from the UI task at the LVGL handler rate.
     */
    void poll();

    /**
     * LVGL input device read callback — do not call directly.
     */
    void readCallback(lv_indev_drv_t* drv, lv_indev_data_t* data);

} // namespace TouchDriver
