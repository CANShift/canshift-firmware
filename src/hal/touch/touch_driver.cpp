// touch_driver.cpp — XPT2046 resistive touch HAL
// In sim mode: dummy input device that always reports "released".
// In hardware mode: XPT2046 via TFT_eSPI built-in touch support.

#include "touch_driver.h"
#include "app_config.h"
#include "diag/logger.h"

#include <lvgl.h>

static lv_indev_drv_t s_indevDrv;

// ---------------------------------------------------------------------------
// HARDWARE MODE
// ---------------------------------------------------------------------------

#if !APP_SIMULATION_MODE

    #include "board_config.h"
    #include "hardware_profile.h"
    #include <TFT_eSPI.h>

// Re-use a TFT_eSPI instance for touch — shares the SPI bus with the display.
// TODO: Replace with a shared global TFT_eSPI instance to avoid duplicate objects.
static TFT_eSPI s_touch;

void TouchDriver::readCallback(lv_indev_drv_t * /*drv*/, lv_indev_data_t *data) {
    uint16_t rawX = 0, rawY = 0;
    const bool pressed = s_touch.getTouch(&rawX, &rawY, 40 /* minimum Z pressure */);

    if (pressed) {
        // TFT_eSPI getTouch() applies calibration if calibrateTouch() was called.
        // TODO: Call s_touch.calibrateTouch() once and persist calibration values.
        data->point.x = static_cast<lv_coord_t>(rawX);
        data->point.y = static_cast<lv_coord_t>(rawY);
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void TouchDriver::init() {
    LOG_INFO("TOUCH", "Initializing touch controller...");

    lv_indev_drv_init(&s_indevDrv);
    s_indevDrv.type = LV_INDEV_TYPE_POINTER;
    s_indevDrv.read_cb = readCallback;

    // Swipe detection thresholds — raise gesture_limit to reduce accidental swipes.
    s_indevDrv.gesture_limit = 40;
    s_indevDrv.gesture_min_velocity = 3;

    lv_indev_drv_register(&s_indevDrv);
    LOG_INFO("TOUCH", "Touch driver registered");
}

void TouchDriver::poll() {
    // lv_task_handler() calls all registered input device read callbacks automatically.
    // This function exists as a hook for future IRQ-driven debounce logic.
}

// ---------------------------------------------------------------------------
// SIMULATION MODE — no hardware, always reports "released"
// ---------------------------------------------------------------------------

#else // APP_SIMULATION_MODE

void TouchDriver::readCallback(lv_indev_drv_t * /*drv*/, lv_indev_data_t *data) {
    data->state = LV_INDEV_STATE_RELEASED;
}

void TouchDriver::init() {
    lv_indev_drv_init(&s_indevDrv);
    s_indevDrv.type = LV_INDEV_TYPE_POINTER;
    s_indevDrv.read_cb = readCallback;
    lv_indev_drv_register(&s_indevDrv);
    LOG_INFO("TOUCH", "Sim mode — touch stub active (always released)");
}

void TouchDriver::poll() {}

#endif // APP_SIMULATION_MODE
