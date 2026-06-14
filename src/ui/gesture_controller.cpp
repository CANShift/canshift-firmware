#include "gesture_controller.h"

#include "app_config.h"
#include "diag/logger.h"
#include "ui/settings_page.h"

#include <Arduino.h>
#include <stdlib.h>

namespace GestureController {

namespace {

constexpr int16_t DRAG_HOTZONE_PX = 40;

constexpr int16_t DRAG_START_THRESHOLD_PX = 6;

SwipeHandler s_swipeHandler = nullptr;
VerticalSwipeHandler s_verticalSwipeHandler = nullptr;
bool s_gesturePressStartedOnClickable = false;

void onGesture(lv_dir_t dir) {
    if (SettingsPage::isOpen() || SettingsPage::isDragging())
        return;

    if (s_gesturePressStartedOnClickable)
        return;

    switch (dir) {
        case LV_DIR_LEFT:
        case LV_DIR_RIGHT:
            if (s_swipeHandler) {
                s_swipeHandler(dir);
            }
            break;
        case LV_DIR_TOP:
        case LV_DIR_BOTTOM:
            if (s_verticalSwipeHandler) {
                s_verticalSwipeHandler(dir);
            }
            break;
        default:
            break;
    }
}

struct DragState {
    bool active = false;
    bool tracking = false;
    int16_t startY = 0;
    int16_t startPanelY = 0;
};

DragState s_drag;

void resetDrag() {
    s_drag = DragState{};
    SettingsPage::setDragging(false);
}

bool s_dragCrossedThreshold = false;

void onDragRelease() {
    if (!s_drag.tracking) {
        resetDrag();
        return;
    }

    const int16_t closedY = SettingsPage::getClosedY();
    const bool startedClosed = (s_drag.startPanelY == closedY);

    if (startedClosed) {
        if (s_dragCrossedThreshold)
            SettingsPage::snapOpen();
        else
            SettingsPage::snapClosed();
    } else {
        if (s_dragCrossedThreshold)
            SettingsPage::snapClosed();
        else
            SettingsPage::snapOpen();
    }

    resetDrag();
}

void updateDrag(lv_indev_t *indev, lv_indev_state_t state) {
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    if (state == LV_INDEV_STATE_RELEASED) {
        if (s_drag.active) {
            const bool wasTracking = s_drag.tracking;
            onDragRelease();

            if (wasTracking) {
                lv_indev_reset_long_press(indev);
                lv_indev_reset(indev, nullptr);
            }
        }
        return;
    }

    if (!s_drag.active) {
        s_drag.active = true;
        s_drag.tracking = false;
        s_drag.startY = p.y;
        s_dragCrossedThreshold = false;

        const bool isOpen = SettingsPage::isOpen();

        if (p.y > DRAG_HOTZONE_PX) {
            s_drag.active = false;
            return;
        }

        s_drag.startPanelY = isOpen ? SettingsPage::getOpenY() : SettingsPage::getClosedY();
    }

    if (!s_drag.active)
        return;

    const int16_t deltaY = p.y - s_drag.startY;

    if (!s_drag.tracking) {
        if (abs(deltaY) < DRAG_START_THRESHOLD_PX)
            return;

        const bool isOpen = SettingsPage::isOpen();
        if (!isOpen && deltaY <= 0)
            return;
        if (isOpen && deltaY >= 0)
            return;

        s_drag.tracking = true;
        SettingsPage::setDragging(true);
        LOG_VDEBUG("UI", "Drag: settings tracker armed (open=%d)", static_cast<int>(isOpen));
    }

    int16_t targetY = static_cast<int16_t>(s_drag.startPanelY + deltaY);
    SettingsPage::setPanelY(targetY);

    const int16_t closedY = SettingsPage::getClosedY();
    const int16_t openY = SettingsPage::getOpenY();
    const int16_t midpointY = closedY + (openY - closedY) / 2;
    const bool startedClosed = (s_drag.startPanelY == closedY);
    s_dragCrossedThreshold = startedClosed ? (targetY >= midpointY) : (targetY <= midpointY);
}

bool s_swipeFiredThisPress = false;

void cancelClickIfSwiping(lv_indev_t *indev, lv_indev_state_t state) {
    static int16_t s_pressStartX = 0;
    static bool s_pressActive = false;
    static bool s_pressCancelled = false;

    if (state == LV_INDEV_STATE_RELEASED) {
        s_pressActive = false;
        s_pressCancelled = false;
        s_swipeFiredThisPress = false;
        s_gesturePressStartedOnClickable = false;
        return;
    }

    lv_point_t p;
    lv_indev_get_point(indev, &p);

    if (!s_pressActive) {
        s_pressActive = true;
        s_pressCancelled = false;
        s_swipeFiredThisPress = false;
        s_pressStartX = p.x;
        lv_obj_t *pressedObj = lv_indev_get_obj_act();
        s_gesturePressStartedOnClickable =
            pressedObj != nullptr && lv_obj_has_flag(pressedObj, LV_OBJ_FLAG_CLICKABLE);
        return;
    }

    if (s_pressCancelled)
        return;

    if (SettingsPage::isOpen() || SettingsPage::isDragging())
        return;

    const int16_t signedTravelX = static_cast<int16_t>(p.x - s_pressStartX);
    const int16_t travelX = static_cast<int16_t>(abs(signedTravelX));
    if (travelX < SWIPE_CANCEL_THRESHOLD_PX)
        return;

    if (s_gesturePressStartedOnClickable)
        return;

    lv_indev_reset_long_press(indev);
    lv_indev_reset(indev, nullptr);
    s_pressCancelled = true;

    if (!s_swipeFiredThisPress && s_swipeHandler) {
        const lv_dir_t dir = signedTravelX < 0 ? LV_DIR_LEFT : LV_DIR_RIGHT;
        s_swipeFiredThisPress = true;
        s_swipeHandler(dir);
    }
    LOG_VDEBUG("UI", "Swipe cancelled pending click (travelX=%d)", travelX);
}

} // namespace

void setSwipeHandler(SwipeHandler handler) {
    s_swipeHandler = handler;
}

void setVerticalSwipeHandler(VerticalSwipeHandler handler) {
    s_verticalSwipeHandler = handler;
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

            updateDrag(indev, state);
            cancelClickIfSwiping(indev, state);

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
