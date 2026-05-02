// touch_driver.cpp — XPT2046 resistive touch HAL
// In sim mode: dummy input device that always reports "released".
// In hardware mode: XPT2046 via TFT_eSPI built-in touch support.
// Calibration data persisted in NVS (namespace "touch", key "cal", 10 bytes).

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
    #include <Preferences.h>

// Re-use a TFT_eSPI instance for touch — shares the SPI bus with the display.
// TODO: Replace with a shared global TFT_eSPI instance to avoid duplicate objects.
static TFT_eSPI s_touch;

static constexpr char NVS_NS[]        = "touch";
static constexpr char NVS_KEY_CAL[]   = "cal";
static constexpr size_t CAL_DATA_SIZE = 5 * sizeof(uint16_t); // 10 bytes

void TouchDriver::readCallback(lv_indev_drv_t * /*drv*/, lv_indev_data_t *data) {
    uint16_t rawX = 0, rawY = 0;
    const bool pressed = s_touch.getTouch(&rawX, &rawY, 40 /* minimum Z pressure */);

    if (pressed) {
        data->point.x = static_cast<lv_coord_t>(rawX);
        data->point.y = static_cast<lv_coord_t>(rawY);
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void TouchDriver::init() {
    LOG_INFO("TOUCH", "Initializing touch controller...");

    // Try loading stored calibration; fall back to board_config.h defaults
    uint16_t calData[5] = {
        TOUCH_CAL_X_MIN, TOUCH_CAL_X_MAX,
        TOUCH_CAL_Y_MIN, TOUCH_CAL_Y_MAX,
        0
    };

    Preferences p;
    p.begin(NVS_NS, /*readOnly=*/true);
    if (p.getBytesLength(NVS_KEY_CAL) == CAL_DATA_SIZE) {
        p.getBytes(NVS_KEY_CAL, calData, CAL_DATA_SIZE);
        p.end();
        s_touch.setTouchCalibrate(calData);
        LOG_INFO("TOUCH", "Calibration loaded from NVS");
    } else {
        p.end();
        s_touch.setTouchCalibrate(calData);
        LOG_WARN("TOUCH",
                 "No NVS calibration — using board_config.h defaults. "
                 "Run Settings → Calibrate Touch for accuracy.");
    }

    lv_indev_drv_init(&s_indevDrv);
    s_indevDrv.type = LV_INDEV_TYPE_POINTER;
    s_indevDrv.read_cb = readCallback;
    s_indevDrv.gesture_limit = 40;
    s_indevDrv.gesture_min_velocity = 3;

    lv_indev_drv_register(&s_indevDrv);
    LOG_INFO("TOUCH", "Touch driver registered");
}

void TouchDriver::poll() {
    // lv_task_handler() calls all registered read callbacks automatically.
}

bool TouchDriver::isCalibrated() {
    Preferences p;
    p.begin(NVS_NS, /*readOnly=*/true);
    const bool has = (p.getBytesLength(NVS_KEY_CAL) == CAL_DATA_SIZE);
    p.end();
    return has;
}

void TouchDriver::calibrate() {
    LOG_INFO("TOUCH", "Starting touch calibration...");
    uint16_t calData[5] = {};
    // TFT_eSPI shows a crosshair sequence and fills calData[5]
    s_touch.calibrateTouch(calData, TFT_WHITE, TFT_BLACK, 15);

    Preferences p;
    p.begin(NVS_NS, /*readOnly=*/false);
    p.putBytes(NVS_KEY_CAL, calData, CAL_DATA_SIZE);
    p.end();

    s_touch.setTouchCalibrate(calData);
    LOG_INFO("TOUCH", "Calibration complete and saved to NVS");
}

void TouchDriver::resetCalibration() {
    Preferences p;
    p.begin(NVS_NS, /*readOnly=*/false);
    p.remove(NVS_KEY_CAL);
    p.end();
    LOG_INFO("TOUCH", "Calibration data cleared from NVS");
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

bool TouchDriver::isCalibrated() { return false; }
void TouchDriver::calibrate() {}
void TouchDriver::resetCalibration() {}

#endif // APP_SIMULATION_MODE
