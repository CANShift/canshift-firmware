// touch_driver.cpp — XPT2046 resistive touch HAL (LovyanGFX backend)
// XPT2046 via the shared LGFX panel.
// Calibration data persisted in NVS (namespace "touch", key "cal", 16 bytes).

#include "touch_driver.h"
#include "app_config.h"
#include "diag/logger.h"
#include "diag/perf_counters.h"
#include "diag/touch_latency.h"

#include <lvgl.h>

static lv_indev_drv_t s_indevDrv;

#include "board_config.h"
#include "hardware_profile.h"
#include "hal/display/display_driver.h"
#include <Preferences.h>

#define s_lcd DisplayDriver::getDisplay()

static constexpr char NVS_NS[] = "touch";
static constexpr char NVS_KEY_CAL[] = "cal";
// LovyanGFX calibration: 8 uint16_t (4 corner pairs). Old TFT_eSPI builds
// stored 10 bytes (5 uint16_t) — those entries fail this size check and
// fall back to defaults, prompting the user to recalibrate once.
static constexpr size_t CAL_DATA_SIZE = 8 * sizeof(uint16_t); // 16 bytes

// Invoked from lv_task_handler() — the UI-task caller already holds g_lvglMutex.
void TouchDriver::readCallback(lv_indev_drv_t * /*drv*/, lv_indev_data_t *data) {
    int32_t x = 0, y = 0;
    const bool pressed = s_lcd.getTouch(&x, &y);

    static bool s_wasPressed = false;
    if (pressed && !s_wasPressed) {
        PERF_RECORD_TOUCH_PRESS();
        TouchLatency::recordPressNow();
    }
    s_wasPressed = pressed;

    if (pressed) {
        // Calibration can overshoot at the edges (e.g. x=353 / y=-8 when
        // pressing top-right corner), causing edge widgets like the gear
        // button to never receive events. Snap to the nearest pixel.
        if (x < 0)
            x = 0;
        if (y < 0)
            y = 0;
        if (x >= HW_DISPLAY_WIDTH)
            x = HW_DISPLAY_WIDTH - 1;
        if (y >= HW_DISPLAY_HEIGHT)
            y = HW_DISPLAY_HEIGHT - 1;
        data->point.x = static_cast<lv_coord_t>(x);
        data->point.y = static_cast<lv_coord_t>(y);
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void TouchDriver::init() {
    LOG_INFO("TOUCH", "Initializing touch controller...");

    Preferences p;
    p.begin(NVS_NS, /*readOnly=*/true);
    const bool hasCalibration = (p.getBytesLength(NVS_KEY_CAL) == CAL_DATA_SIZE);
    if (hasCalibration) {
        uint16_t calData[8] = {};
        p.getBytes(NVS_KEY_CAL, calData, CAL_DATA_SIZE);
        s_lcd.setTouchCalibrate(calData);
        LOG_INFO("TOUCH", "Calibration loaded from NVS");
    }
    p.end();

    // Without a valid calibration the panel returns raw ADC values that
    // never match screen coords — the user cannot reach Settings to
    // calibrate manually. Run the interactive routine on first boot so
    // the device is usable out of the box.
    if (!hasCalibration) {
        LOG_WARN("TOUCH", "No NVS calibration — running first-boot calibration");
        calibrate();
    }

    lv_indev_drv_init(&s_indevDrv);
    s_indevDrv.type = LV_INDEV_TYPE_POINTER;
    s_indevDrv.read_cb = readCallback;
    s_indevDrv.gesture_limit = 40;
    s_indevDrv.gesture_min_velocity = 3;

    lv_indev_drv_register(&s_indevDrv);

    // Global click listener: bubble-up LV_EVENT_CLICKED from any screen lands
    // here so we can close the press → click latency loop. Attaching to
    // lv_layer_top() works because that layer's event tree sees every
    // dispatched click on the active display.
    //
    // The lambda always fires the production-shipped warn-on-slow path
    // (issue #1256), and feeds the full histogram on dev builds via the
    // existing APP_PROFILE_UI gate.
    lv_obj_add_event_cb(
        lv_layer_top(),
        [](lv_event_t * /*e*/) {
            TouchLatency::consumePressAndWarnIfSlow();
#if APP_PROFILE_UI
            uint32_t deltaUs = 0;
            if (::PerfCounters::consumeTouchPressTs(&deltaUs)) {
                ::PerfCounters::recordSample(::PerfCounters::TOUCH_LATENCY, deltaUs);
            }
#endif
        },
        LV_EVENT_CLICKED, nullptr);

    LOG_INFO("TOUCH", "Touch driver registered");
}

void TouchDriver::poll() {}

bool TouchDriver::isCalibrated() {
    Preferences p;
    p.begin(NVS_NS, /*readOnly=*/true);
    const bool has = (p.getBytesLength(NVS_KEY_CAL) == CAL_DATA_SIZE);
    p.end();
    return has;
}

void TouchDriver::calibrate() {
    LOG_INFO("TOUCH", "Starting touch calibration...");
    uint16_t calData[8] = {};
    // LovyanGFX shows 4 corner crosshairs and fills calData[8].
    s_lcd.calibrateTouch(calData, TFT_WHITE, TFT_BLACK,
                         std::max(s_lcd.width(), s_lcd.height()) >> 3);

    Preferences p;
    p.begin(NVS_NS, /*readOnly=*/false);
    p.putBytes(NVS_KEY_CAL, calData, CAL_DATA_SIZE);
    p.end();

    s_lcd.setTouchCalibrate(calData);
    LOG_INFO("TOUCH", "Calibration complete and saved to NVS");
}

void TouchDriver::resetCalibration() {
    Preferences p;
    p.begin(NVS_NS, /*readOnly=*/false);
    p.remove(NVS_KEY_CAL);
    p.end();
    LOG_INFO("TOUCH", "Calibration data cleared from NVS");
}
