#include "ui/display_idle.h"

#include "alert_engine_rs.h"
#include "app_config.h"
#include "can/can_manager.h"
#include "diag/logger.h"
#include "hal/display/display_driver.h"
#include "hal/power/cpu_clock.h"
#include "ui/alert_takeover.h"
#include "ui/settings_page.h"

#include <lvgl.h>

static_assert(kAlertTouchReadActiveMs == LV_INDEV_DEF_READ_PERIOD,
              "an active panel reads touch at the LVGL default rate");

namespace {

constexpr uint8_t kBacklightMax = 255;

uint8_t s_state = kAlertDisplayIdleLive;

uint8_t backlightFor(uint8_t state) {
    const uint16_t userPercent = SettingsPage::getBrightness();
    if (state == kAlertDisplayIdleOff)
        return 0;
    const uint16_t percent =
        (state == kAlertDisplayIdleDim) ? (userPercent * kDisplayDimPercent) / 100u : userPercent;
    return static_cast<uint8_t>((percent * kBacklightMax) / 100u);
}

uint32_t idleMs() {
    const uint32_t sinceFrame = CanManager::msSinceLastRx();
    const uint32_t sinceTouch = lv_disp_get_inactive_time(nullptr);
    return sinceFrame < sinceTouch ? sinceFrame : sinceTouch;
}

lv_indev_t *touchIndev() {
    for (lv_indev_t *indev = lv_indev_get_next(nullptr); indev != nullptr;
         indev = lv_indev_get_next(indev)) {
        if (indev->driver->type == LV_INDEV_TYPE_POINTER)
            return indev;
    }
    return nullptr;
}

void suppressTouchThatWokeThePanel() {
    lv_indev_t *indev = touchIndev();
    if (indev != nullptr)
        lv_indev_wait_release(indev);
}

void paceTouchReads() {
    lv_indev_t *indev = touchIndev();
    if (indev == nullptr || indev->driver->read_timer == nullptr)
        return;
    const uint32_t period =
        alert_touch_read_period_ms_rs(s_state, lv_disp_get_inactive_time(nullptr));
    if (indev->driver->read_timer->period != period)
        lv_timer_set_period(indev->driver->read_timer, period);
}

void pauseRedraws(bool paused) {
    lv_disp_t *disp = lv_disp_get_default();
    if (disp == nullptr || disp->refr_timer == nullptr)
        return;
    if (paused)
        lv_timer_pause(disp->refr_timer);
    else
        lv_timer_resume(disp->refr_timer);
}

void darkenPanel() {
    pauseRedraws(true);
    CpuClock::enterLowPower();
}

void relightPanel() {
    CpuClock::leaveLowPower();
    pauseRedraws(false);
    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(nullptr);
}

const char *stateName(uint8_t state) {
    if (state == kAlertDisplayIdleOff)
        return "off";
    if (state == kAlertDisplayIdleDim)
        return "dim";
    return "live";
}

void apply(uint8_t next) {
    if (next == s_state)
        return;
    const bool wasOff = s_state == kAlertDisplayIdleOff;
    s_state = next;
    if (wasOff)
        relightPanel();
    DisplayDriver::setBacklight(backlightFor(next));
    if (next == kAlertDisplayIdleOff)
        darkenPanel();
    LOG_INFO("IDLE", "display %s (idle=%lus)", stateName(next),
             static_cast<unsigned long>(idleMs() / 1000u));
}

}

namespace DisplayIdle {

void init() {
    s_state = kAlertDisplayIdleLive;
}

void update() {
    const uint8_t next = alert_display_idle_state_rs(
        idleMs(), kDisplayDimAfterIdleMs, kDisplayOffAfterIdleMs, AlertTakeover::isActive());
    if (s_state == kAlertDisplayIdleOff && next == kAlertDisplayIdleLive)
        suppressTouchThatWokeThePanel();
    apply(next);
    paceTouchReads();
}

void wake() {
    lv_disp_trig_activity(nullptr);
    apply(kAlertDisplayIdleLive);
}

bool isPanelOff() {
    return s_state == kAlertDisplayIdleOff;
}

}
