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
#include <esp_task_wdt.h>

#define s_lcd DisplayDriver::getDisplay()

static constexpr char NVS_NS[] = "touch";
static constexpr char NVS_KEY_CAL[] = "cal";

static constexpr size_t CAL_DATA_SIZE = 8 * sizeof(uint16_t);

void TouchDriver::readCallback(lv_indev_drv_t *, lv_indev_data_t *data) {
    int32_t x = 0, y = 0;
    const bool pressed = s_lcd.getTouch(&x, &y);

    static bool s_wasPressed = false;
    if (pressed && !s_wasPressed) {
        PERF_RECORD_TOUCH_PRESS();
        TouchLatency::recordPressNow();
    }
    s_wasPressed = pressed;

    if (pressed) {

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
    p.begin(NVS_NS, true);
    const bool hasCalibration = (p.getBytesLength(NVS_KEY_CAL) == CAL_DATA_SIZE);
    if (hasCalibration) {
        uint16_t calData[8] = {};
        p.getBytes(NVS_KEY_CAL, calData, CAL_DATA_SIZE);
        s_lcd.setTouchCalibrate(calData);
        LOG_INFO("TOUCH", "Calibration loaded from NVS");
    }
    p.end();

    if (!hasCalibration) {
        LOG_WARN("TOUCH", "No NVS calibration — running first-boot calibration");
        calibrate();
    }

    lv_indev_drv_init(&s_indevDrv);
    s_indevDrv.type = LV_INDEV_TYPE_POINTER;
    s_indevDrv.read_cb = readCallback;
    s_indevDrv.gesture_limit = 40;
    s_indevDrv.gesture_min_velocity = 3;
    s_indevDrv.scroll_limit = 8;

    lv_indev_drv_register(&s_indevDrv);

    lv_obj_add_event_cb(
        lv_layer_top(),
        [](lv_event_t *) {
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
    p.begin(NVS_NS, true);
    const bool has = (p.getBytesLength(NVS_KEY_CAL) == CAL_DATA_SIZE);
    p.end();
    return has;
}

void TouchDriver::calibrate() {
    LOG_INFO("TOUCH", "Starting touch calibration...");
    uint16_t calData[8] = {};

    // calibrateTouch() blocks until the user taps all 4 corners, with no hook to
    // feed the WDT from inside — unsubscribe the calling task for the duration so
    // a calibration taking longer than TASK_WDT_TIMEOUT_MS does not panic.
    const bool wasWdtSubscribed = (esp_task_wdt_delete(nullptr) == ESP_OK);

    s_lcd.calibrateTouch(calData, TFT_WHITE, TFT_BLACK,
                         std::max(s_lcd.width(), s_lcd.height()) >> 3);

    if (wasWdtSubscribed) {
        const esp_err_t err = esp_task_wdt_add(nullptr);
        if (err != ESP_OK)
            LOG_WARN("TOUCH", "WDT re-subscribe failed: %d", static_cast<int>(err));
    }

    Preferences p;
    p.begin(NVS_NS, false);
    p.putBytes(NVS_KEY_CAL, calData, CAL_DATA_SIZE);
    p.end();

    s_lcd.setTouchCalibrate(calData);
    LOG_INFO("TOUCH", "Calibration complete and saved to NVS");
}

void TouchDriver::resetCalibration() {
    Preferences p;
    p.begin(NVS_NS, false);
    p.remove(NVS_KEY_CAL);
    p.end();
    LOG_INFO("TOUCH", "Calibration data cleared from NVS");
}
