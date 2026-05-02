#pragma once
// touch_driver.h — XPT2046 resistive touch HAL
//
// Wraps TFT_eSPI's built-in XPT2046 support and registers an LVGL input device.
//
// Calibration:
//   On first boot (no NVS data): uses board_config.h TOUCH_CAL_* defaults.
//   After running calibrate(): 5×uint16_t stored in NVS namespace "touch".
//   Run calibrate() from the settings page whenever touch accuracy is poor.

#include <lvgl.h>

namespace TouchDriver {

/**
 * Initialize the XPT2046 touch controller.
 * Loads calibration from NVS if available, otherwise uses board_config.h defaults.
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
void readCallback(lv_indev_drv_t *drv, lv_indev_data_t *data);

/**
 * Returns true if touch calibration data is stored in NVS.
 * False on first boot or after resetCalibration().
 */
bool isCalibrated();

/**
 * Run the interactive touch calibration routine (TFT_eSPI crosshair touch).
 * Stores 5 calibration points to NVS and applies them immediately.
 * Must be called from the UI task (LVGL mutex held).
 * Only available in hardware mode — no-op in sim mode.
 */
void calibrate();

/**
 * Clear NVS calibration data.
 * board_config.h TOUCH_CAL_* defaults are used on next boot.
 */
void resetCalibration();

} // namespace TouchDriver
