// touch_driver.cpp — XPT2046 resistive touch HAL

#include "touch_driver.h"
#include "board_config.h"
#include "hardware_profile.h"
#include "diag/logger.h"

#include <TFT_eSPI.h>
#include <lvgl.h>

// TFT_eSPI provides a companion XPT2046_Touchscreen or uses its own touch
// functions. We use TFT_eSPI's built-in touch support.
// TODO: Confirm TFT_eSPI is compiled with TOUCH_CS defined (matches PIN_TOUCH_CS).

static TFT_eSPI s_touch;   // Re-use the same TFT instance — touch shares SPI bus
                            // TODO: Use a shared global TFT_eSPI instance rather than
                            //       separate instances in display and touch drivers.

static lv_indev_drv_t s_indevDrv;

void TouchDriver::readCallback(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    uint16_t rawX, rawY;
    bool pressed = s_touch.getTouch(&rawX, &rawY, 40 /* minimum Z pressure */);

    if (pressed) {
        // Map raw ADC values to screen coordinates using calibration values
        // TFT_eSPI getTouch() already applies calibration if calibrateTouch() has been called.
        // If not calibrated, map manually:
        // int32_t screenX = map(rawX, TOUCH_CAL_X_MIN, TOUCH_CAL_X_MAX, 0, HW_DISPLAY_WIDTH  - 1);
        // int32_t screenY = map(rawY, TOUCH_CAL_Y_MIN, TOUCH_CAL_Y_MAX, 0, HW_DISPLAY_HEIGHT - 1);

        data->point.x = static_cast<lv_coord_t>(rawX);
        data->point.y = static_cast<lv_coord_t>(rawY);
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void TouchDriver::init() {
    LOG_INFO("TOUCH", "Initializing touch controller...");

    // TODO: Call s_touch.calibrateTouch() once to determine calibration values,
    //       store them, then use setTouchCalibrate() on subsequent boots.
    // For now: use default calibration (may require manual mapping above).

    // Register LVGL input device
    lv_indev_drv_init(&s_indevDrv);
    s_indevDrv.type    = LV_INDEV_TYPE_POINTER;
    s_indevDrv.read_cb = readCallback;
    lv_indev_drv_register(&s_indevDrv);

    LOG_INFO("TOUCH", "Touch driver registered");
}

void TouchDriver::poll() {
    // lv_task_handler() calls all registered input device read callbacks
    // automatically, so explicit poll is a no-op here.
    // This function exists as a hook for future IRQ-driven touch debounce logic.
}
