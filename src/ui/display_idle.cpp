#include "ui/display_idle.h"

#include "alert_engine_rs.h"
#include "app_config.h"
#include "can/can_manager.h"
#include "diag/logger.h"
#include "hal/display/display_driver.h"
#include "ui/settings_page.h"

#include <lvgl.h>

namespace {

constexpr uint8_t kBacklightMax = 255;

uint8_t s_state = ALERT_DISPLAY_IDLE_LIVE;

uint8_t backlightFor(uint8_t state) {
    const uint16_t userPercent = SettingsPage::getBrightness();
    if (state == ALERT_DISPLAY_IDLE_OFF)
        return 0;
    const uint16_t percent = (state == ALERT_DISPLAY_IDLE_DIM)
                                 ? (userPercent * DISPLAY_DIM_PERCENT) / 100u
                                 : userPercent;
    return static_cast<uint8_t>((percent * kBacklightMax) / 100u);
}

uint32_t idleMs() {
    const uint32_t sinceFrame = CanManager::msSinceLastRx();
    const uint32_t sinceTouch = lv_disp_get_inactive_time(nullptr);
    return sinceFrame < sinceTouch ? sinceFrame : sinceTouch;
}

void suppressTouchThatWokeThePanel() {
    lv_indev_t *indev = lv_indev_get_act();
    if (indev != nullptr)
        lv_indev_wait_release(indev);
}

const char *stateName(uint8_t state) {
    if (state == ALERT_DISPLAY_IDLE_OFF)
        return "off";
    if (state == ALERT_DISPLAY_IDLE_DIM)
        return "dim";
    return "live";
}

} // namespace

namespace DisplayIdle {

void init() {
    s_state = ALERT_DISPLAY_IDLE_LIVE;
}

void update() {
    const uint8_t next =
        alert_display_idle_state_rs(idleMs(), DISPLAY_DIM_AFTER_IDLE_MS, DISPLAY_OFF_AFTER_IDLE_MS);
    if (next == s_state)
        return;

    if (s_state == ALERT_DISPLAY_IDLE_OFF && next == ALERT_DISPLAY_IDLE_LIVE)
        suppressTouchThatWokeThePanel();

    s_state = next;
    DisplayDriver::setBacklight(backlightFor(next));
    LOG_INFO("IDLE", "display %s (idle=%lus)", stateName(next),
             static_cast<unsigned long>(idleMs() / 1000u));
}

bool isPanelOff() {
    return s_state == ALERT_DISPLAY_IDLE_OFF;
}

} // namespace DisplayIdle
