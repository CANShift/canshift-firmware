#pragma once
// touch_driver.h — XPT2046 resistive touch HAL
//
// Uses LovyanGFX's built-in XPT2046 support (shared LGFX panel) and
// registers an LVGL input device.
//
// Calibration:
//   On first boot (no NVS data): runs the interactive 4-corner routine.
//   After running calibrate(): 8×uint16_t (16 bytes) stored in NVS namespace
//   "touch" under key "cal". Survives reboots and OTA app updates because the
//   NVS partition is preserved.
//   Run calibrate() from the settings page whenever touch accuracy is poor.
//   Run resetCalibration() to wipe the saved offsets — next boot falls back to
//   the first-boot interactive routine.

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
 * Run the interactive touch calibration routine (LovyanGFX 4-corner touch).
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
