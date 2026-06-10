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

// LVGL 8.3 gesture detection lives in the indev layer — reading
// lv_indev_get_gesture_dir() after lv_task_handler is the reliable path even
// when buttons absorb the touch event and prevent LV_EVENT_GESTURE bubbling.
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

// Drag tracker state — reset on every touch release.
struct DragState {
    bool active = false;     // True between press and release.
    bool tracking = false;   // True once we've decided this is our gesture.
    int16_t startY = 0;      // Touch y where the press began.
    int16_t startPanelY = 0; // s_panel y at gesture start (open or closed Y).
};

DragState s_drag;

void resetDrag() {
    s_drag = DragState{};
    SettingsPage::setDragging(false);
}

// Crossed-threshold flag — written by updateDrag() while the gesture is in
// flight, read by onDragRelease() to decide which way to snap.
bool s_dragCrossedThreshold = false;

void onDragRelease() {
    if (!s_drag.tracking) {
        resetDrag();
        return;
    }

    // Snap based on the issue spec ("drag > 50% of panel height"):
    //  - Started closed: cross midpoint downward → open, else fall back closed.
    //  - Started open:   cross midpoint upward   → close, else fall back open.
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
            // Settings swipe just released — clear the indev so LVGL's velocity-
            // based gesture latch doesn't fire LV_DIR_TOP/BOTTOM on the next
            // checkGestures tick. Without this, a swipe-up that closes settings
            // also opens the diag drawer because both react to LV_DIR_TOP.
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
        // The drag is only armed when the press starts in the top hot-zone.
        // From closed, this is the band just under the top edge; from open,
        // it's the top bar itself — both at p.y < DRAG_HOTZONE_PX. Anywhere
        // else inside the open panel keeps its normal click/scroll behavior.
        if (p.y > DRAG_HOTZONE_PX) {
            s_drag.active = false;
            return; // Not our gesture.
        }

        s_drag.startPanelY = isOpen ? SettingsPage::getOpenY() : SettingsPage::getClosedY();
    }

    if (!s_drag.active)
        return;

    const int16_t deltaY = p.y - s_drag.startY;

    // Latch into "tracking" only once the finger has moved enough — below the
    // threshold the press is treated as a tap so buttons / sliders inside the
    // panel still work normally when Settings is open.
    if (!s_drag.tracking) {
        if (abs(deltaY) < DRAG_START_THRESHOLD_PX)
            return;

        const bool isOpen = SettingsPage::isOpen();
        // From closed, only downward drags qualify; from open, only upward.
        if (!isOpen && deltaY <= 0)
            return;
        if (isOpen && deltaY >= 0)
            return;

        s_drag.tracking = true;
        SettingsPage::setDragging(true);
        LOG_VDEBUG("UI", "Drag: settings tracker armed (open=%d)", static_cast<int>(isOpen));
    }

    // Translate panel position 1:1 with the finger.
    int16_t targetY = static_cast<int16_t>(s_drag.startPanelY + deltaY);
    SettingsPage::setPanelY(targetY);

    // Threshold = midpoint between closed and open. Once crossed in either
    // direction we remember it for the release decision.
    const int16_t closedY = SettingsPage::getClosedY();
    const int16_t openY = SettingsPage::getOpenY();
    const int16_t midpointY = closedY + (openY - closedY) / 2;
    const bool startedClosed = (s_drag.startPanelY == closedY);
    if (startedClosed) {
        // Crossed = pulled past midpoint (user wants to open).
        s_dragCrossedThreshold = (targetY >= midpointY);
    } else {
        // Crossed = pushed past midpoint upward (user wants to close).
        s_dragCrossedThreshold = (targetY <= midpointY);
    }
}

// Per-press swipe-fired latch — set when `cancelClickIfSwiping` dispatches
// the page swipe, cleared on release. Read by `checkGestures` below to skip
// the LVGL-level gesture re-fire that would otherwise navigate twice for a
// single finger motion.
bool s_swipeFiredThisPress = false;

// Track horizontal travel since press-down. If it exceeds the swipe cancel
// threshold we clear pressed state on the underlying object so its click
// handler does not fire on release — the press has clearly become a swipe.
// Issue #640: a swipe whose path crosses a button widget previously triggered
// the button on lift-up because LVGL routes touch events to the object under
// the press-down point regardless of finger movement.
//
// Issue #1262: also fire the page-swipe directly from here so navigation
// commits on the same threshold that cancels the click. Waiting for LVGL's
// own `gesture_limit` (40 px cumulative) left a 12-40 px dead zone where the
// click was cancelled but no swipe fired — the page sat still and the user
// thought the swipe was lost.
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

    // Defer to the settings drag tracker — its hot-zone press lives in the
    // top 40 px of the screen and any horizontal drift on the way down used
    // to cross the 8 px swipe threshold below and steal the gesture, leaving
    // settings refusing to open. Issue #1262 follow-up.
    if (SettingsPage::isOpen() || SettingsPage::isDragging())
        return;

    const int16_t signedTravelX = static_cast<int16_t>(p.x - s_pressStartX);
    const int16_t travelX = static_cast<int16_t>(abs(signedTravelX));
    if (travelX < SWIPE_CANCEL_THRESHOLD_PX)
        return;

    // Reset clears act_obj/last_pressed and sets reset_query so the in-flight
    // touch cycle terminates without dispatching LV_EVENT_CLICKED on release.
    // Also reset long-press timing so a stuck long-press timer can't refire.
    lv_indev_reset_long_press(indev);
    lv_indev_reset(indev, nullptr);
    s_pressCancelled = true;

    // Fire the page-nav swipe immediately so navigation feels snappy on
    // pages where the buttons fill the canvas (issue #1262). Direction
    // mirrors the finger: a leftward drag (negative travel) navigates to
    // the next page (LV_DIR_LEFT in LVGL's gesture convention).
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
