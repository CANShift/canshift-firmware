#include "gesture_controller.h"

#include "app_config.h"
#include "diag/logger.h"
#include "ui/gesture_intent.h"
#include "ui/page_turn_gate.h"
#include "ui/settings_drag.h"
#include "ui/settings_page.h"

#include <Arduino.h>
#include <stdlib.h>

namespace GestureController {

namespace {

SwipeHandler s_swipeHandler = nullptr;

struct PressState {
    bool active = false;
    bool clickCancelled = false;
    bool swipeFired = false;
    bool startedOnClickable = false;
    bool tapEligible = false;
    int16_t startX = 0;
    int16_t startY = 0;
    int16_t lastX = 0;
    int16_t lastY = 0;
};

PressState s_press;

void onGesture(lv_dir_t dir) {
    if (SettingsPage::isOpen() || SettingsPage::isDragging())
        return;

    if (s_press.startedOnClickable)
        return;

    switch (dir) {
        case LV_DIR_LEFT:
        case LV_DIR_RIGHT:
            if (s_swipeHandler) {
                s_swipeHandler(dir);
            }
            break;
        default:
            break;
    }
}

bool pressStartsOnClickable(const lv_indev_t *indev) {
    lv_obj_t *pressed = indev->proc.types.pointer.act_obj;
    return pressed != nullptr && pressed != lv_scr_act() &&
           lv_obj_has_flag(pressed, LV_OBJ_FLAG_CLICKABLE);
}

void beginPress(const lv_indev_t *indev, const lv_point_t &p) {
    s_press = PressState{};
    s_press.active = true;
    s_press.startX = p.x;
    s_press.startY = p.y;
    s_press.lastX = p.x;
    s_press.lastY = p.y;
    s_press.startedOnClickable = pressStartsOnClickable(indev);
    s_press.tapEligible =
        !SettingsPage::isOpen() && !SettingsPage::isDragging() && !SettingsDrag::inHotzone(p.y);
}

int16_t pressTravel() {
    const int16_t travelX = static_cast<int16_t>(abs(s_press.lastX - s_press.startX));
    const int16_t travelY = static_cast<int16_t>(abs(s_press.lastY - s_press.startY));
    return travelX > travelY ? travelX : travelY;
}

bool tapLandsOnDash() {
    if (!s_press.tapEligible || s_press.clickCancelled || s_press.startedOnClickable)
        return false;
    if (SettingsPage::isOpen() || SettingsPage::isDragging())
        return false;
    return PageTurnGate::open();
}

void turnPageOnTap() {
    if (s_swipeHandler == nullptr || !tapLandsOnDash())
        return;
    const GestureIntent::TapTurn turn =
        GestureIntent::tapTurn(s_press.lastX, static_cast<int16_t>(LV_HOR_RES), pressTravel());
    if (turn == GestureIntent::TapTurn::None)
        return;
    LOG_VDEBUG("UI", "Gesture: tap at x=%d turns the page", static_cast<int>(s_press.lastX));
    s_swipeHandler(turn == GestureIntent::TapTurn::Next ? LV_DIR_LEFT : LV_DIR_RIGHT);
}

void closeSettingsOnSwipe(lv_indev_t *indev, int16_t signedTravelX, int16_t signedTravelY) {
    if (s_press.clickCancelled || !GestureIntent::closesSettings(signedTravelY, signedTravelX))
        return;
    // snapClosed, not close: the drag path already slides the panel out and
    // the swipe is the same gesture family, so it uses the same motion
    // rather than a second, instant one.
    SettingsPage::snapClosed();
    // The finger is still down and the page underneath is now exposed —
    // without this the release lands a click on it (#288).
    lv_indev_wait_release(indev);
    s_press.clickCancelled = true;
}

void cancelClickIfSwiping(lv_indev_t *indev) {
    if ((s_press.clickCancelled && s_press.swipeFired) || SettingsPage::isDragging())
        return;

    const int16_t signedTravelX = static_cast<int16_t>(s_press.lastX - s_press.startX);
    const int16_t signedTravelY = static_cast<int16_t>(s_press.lastY - s_press.startY);
    const int16_t travelY = static_cast<int16_t>(abs(signedTravelY));

    if (SettingsPage::isOpen()) {
        closeSettingsOnSwipe(indev, signedTravelX, signedTravelY);
        return;
    }
    const GestureIntent::Decision decision =
        GestureIntent::decide(signedTravelX, travelY, s_press.startedOnClickable);

    if (decision.cancelClick && !s_press.clickCancelled) {
        // wait_until_release is the only sticky cancel: lv_indev_reset clears
        // act_obj but leaves the next poll free to re-press. With the finger
        // still down and the page swapped underneath it, that re-press lands a
        // click on whatever the new page put under the finger.
        lv_indev_wait_release(indev);
        s_press.clickCancelled = true;
        LOG_VDEBUG("UI", "press dropped, travel=%d,%d", static_cast<int>(signedTravelX),
                   static_cast<int>(travelY));
    }

    if (!decision.fireSwipe || s_press.swipeFired || s_swipeHandler == nullptr)
        return;
    s_press.swipeFired = true;
    s_swipeHandler(decision.swipeLeft ? LV_DIR_LEFT : LV_DIR_RIGHT);
}

void trackPress(lv_indev_t *indev, lv_indev_state_t state) {
    if (state == LV_INDEV_STATE_RELEASED) {
        if (s_press.active)
            turnPageOnTap();
        s_press = PressState{};
        return;
    }

    lv_point_t p;
    lv_indev_get_point(indev, &p);

    if (!s_press.active) {
        beginPress(indev, p);
        return;
    }
    s_press.lastX = p.x;
    s_press.lastY = p.y;
    cancelClickIfSwiping(indev);
}

} // namespace

void setSwipeHandler(SwipeHandler handler) {
    s_swipeHandler = handler;
}

void checkGestures() {
    static lv_dir_t lastDir = LV_DIR_NONE;

    lv_indev_t *indev = lv_indev_get_next(nullptr);

    if (indev == nullptr) {
        static bool s_warned = false;
        if (!s_warned) {
            LOG_ERROR("UI", "checkGestures: no indev registered!");
            s_warned = true;
        }
        return;
    }

    while (indev != nullptr) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
            const lv_indev_state_t state = indev->proc.state;

            SettingsDrag::update(indev, state);
            trackPress(indev, state);

            if (state == LV_INDEV_STATE_RELEASED) {
                lastDir = LV_DIR_NONE;
            } else {
                lv_dir_t dir = lv_indev_get_gesture_dir(indev);
                if (dir != LV_DIR_NONE && dir != lastDir) {
                    onGesture(dir);
                    lastDir = dir;
                }
            }
            break;
        }
        indev = lv_indev_get_next(indev);
    }
}

} // namespace GestureController
