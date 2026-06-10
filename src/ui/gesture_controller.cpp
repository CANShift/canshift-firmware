#include "gesture_controller.h"

#include "app_config.h"
#include "diag/logger.h"
#include "ui/settings_page.h"

#include <Arduino.h>
#include <stdlib.h>

namespace GestureController {

namespace {

// Top-bar plus a small overshoot so finger placement on the bar still triggers.
constexpr int16_t DRAG_HOTZONE_PX = 40;
// Below LVGL's gesture threshold so we settle ambiguity before swipe fires.
constexpr int16_t DRAG_START_THRESHOLD_PX = 6;

SwipeHandler s_swipeHandler = nullptr;
VerticalSwipeHandler s_verticalSwipeHandler = nullptr;

// Reading via the indev layer survives buttons absorbing LV_EVENT_GESTURE.
void onGesture(lv_dir_t dir) {
    if (SettingsPage::isOpen() || SettingsPage::isDragging())
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

    // Issue spec: snap to open/closed based on whether drag crossed midpoint.
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
            // Clear LVGL's gesture latch — else a settings-close swipe also opens
            // diag drawer (both react to LV_DIR_TOP).
            if (wasTracking) {
                lv_indev_reset_long_press(indev);
                lv_indev_reset(indev, nullptr);
            }
        }
        return;
    }

    // Pressed.
    if (!s_drag.active) {
        s_drag.active = true;
        s_drag.tracking = false;
        s_drag.startY = p.y;
        s_dragCrossedThreshold = false;

        const bool isOpen = SettingsPage::isOpen();
        // Drag only arms in the top hot-zone — elsewhere the panel handles clicks/scroll.
        if (p.y > DRAG_HOTZONE_PX) {
            s_drag.active = false;
            return;
        }

        s_drag.startPanelY = isOpen ? SettingsPage::getOpenY() : SettingsPage::getClosedY();
    }

    if (!s_drag.active)
        return;

    const int16_t deltaY = p.y - s_drag.startY;

    // Below threshold is treated as a tap so widgets inside the panel work.
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

// Cleared on release. Read by checkGestures to skip LVGL's gesture re-fire
// that would otherwise navigate twice for the same finger motion.
bool s_swipeFiredThisPress = false;

// Past threshold: clear pressed state so click doesn't fire (#640) AND fire
// the page swipe directly to close the dead zone vs LVGL gesture_limit (#1262).
void cancelClickIfSwiping(lv_indev_t *indev, lv_indev_state_t state) {
    static int16_t s_pressStartX = 0;
    static bool s_pressActive = false;
    static bool s_pressCancelled = false;

    if (state == LV_INDEV_STATE_RELEASED) {
        s_pressActive = false;
        s_pressCancelled = false;
        s_swipeFiredThisPress = false;
        return;
    }

    lv_point_t p;
    lv_indev_get_point(indev, &p);

    if (!s_pressActive) {
        s_pressActive = true;
        s_pressCancelled = false;
        s_swipeFiredThisPress = false;
        s_pressStartX = p.x;
        return;
    }

    if (s_pressCancelled)
        return;

    // Defer to settings drag tracker — its hot-zone press conflicts (#1262 fu).
    if (SettingsPage::isOpen() || SettingsPage::isDragging())
        return;

    const int16_t signedTravelX = static_cast<int16_t>(p.x - s_pressStartX);
    const int16_t travelX = static_cast<int16_t>(abs(signedTravelX));
    if (travelX < SWIPE_CANCEL_THRESHOLD_PX)
        return;

    // Terminates the touch cycle so LV_EVENT_CLICKED doesn't fire on release.
    lv_indev_reset_long_press(indev);
    lv_indev_reset(indev, nullptr);
    s_pressCancelled = true;

    // Fire immediately — feels snappier than LVGL's own gesture commit (#1262).
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
