#pragma once
// display_driver.h — ILI9341 display HAL
//
// Wraps TFT_eSPI and provides the LVGL flush callback.
// All display configuration is in board_config.h and lv_conf.h.
//
// TODO: Confirm TFT_eSPI User_Setup.h values match board_config.h pins.
//       TFT_eSPI can be configured via its own User_Setup.h or via build flags.
//       We use build flags (USER_SETUP_LOADED=1) to avoid modifying the library.

#include <lvgl.h>
#include <stdint.h>

namespace DisplayDriver {

/**
     * Initialize the ILI9341 display via TFT_eSPI.
     * Sets up SPI, sends init commands, configures backlight PWM.
     * Must be called before lv_init() and registerWithLVGL().
     */
void init();

/**
     * Register the display driver with LVGL.
     * Creates the LVGL draw buffers and sets the flush callback.
     * Call after lv_init().
     */
void registerWithLVGL();

/**
     * Set backlight brightness (0=off, 255=full).
     */
void setBacklight(uint8_t brightness);

/**
     * LVGL flush callback — do not call directly.
     * Called by LVGL when a screen region needs to be pushed to the display.
     */
void flushCallback(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *colorMap);

} // namespace DisplayDriver
