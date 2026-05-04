#pragma once
// display_driver.h — ILI9341 display HAL
//
// Wraps TFT_eSPI and provides the LVGL flush callback.
// All display configuration is in board_config.h and User_Setup.h.
//
// getTft() exposes the single shared TFT_eSPI instance so that touch_driver
// can call XPT2046 methods on the same object without creating a second instance.

#include <lvgl.h>
#include <stdint.h>

#if !defined(APP_SIMULATION_MODE) || !APP_SIMULATION_MODE
    #include <TFT_eSPI.h>
#endif

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

#if !defined(APP_SIMULATION_MODE) || !APP_SIMULATION_MODE
/**
     * Return the shared TFT_eSPI instance.
     * Used by touch_driver to call XPT2046 methods on the same SPI object.
     */
TFT_eSPI &getTft();
#endif

} // namespace DisplayDriver
