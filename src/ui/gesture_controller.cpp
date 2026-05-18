// gesture_controller.cpp — Page-nav swipe, settings-panel drag, and
// swipe-click cancellation. Extracted from `page_manager.cpp` (issue #704);
// behaviour preserved verbatim, only the swipe path becomes callback-driven
// so PageManager owns "which page to show next" without coupling the
// gesture TU back into page lifecycle.

#include "gesture_controller.h"

#include "app_config.h"
#include "diag/logger.h"
#include "ui/settings_page.h"

#include <Arduino.h>
#include <stdlib.h>

namespace GestureController {

namespace {

// y-coordinate band that triggers the drag from the closed state. Picked to
// be a hair larger than the top bar so a finger landing on the bar can still
// initiate the gesture without needing pixel-perfect aim.
constexpr int16_t DRAG_HOTZONE_PX = 40;

// Movement (in px) below which a press isn't a drag. Below the LVGL gesture
// threshold so we settle ambiguity before the swipe path fires.
constexpr int16_t DRAG_START_THRESHOLD_PX = 6;

SwipeHandler s_swipeHandler = nullptr;

// LVGL 8.3 gesture recognition lives in the indev layer, not in the object
// event system. Reading lv_indev_get_gesture_dir() after lv_task_handler has
// run is the reliable path — it works even when buttons or sliders absorb
// the touch event and prevent LV_EVENT_GESTURE from reaching the screen.

void onGesture(lv_dir_t dir) {
    // Settings drag is handled by the drag tracker — ignore swipe gestures
    // when the panel is open or in motion so we don't double-fire close().
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
        default:
            // Vertical gestures are owned by the drag tracker — see updateDrag().
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
        if (s_drag.active)
            onDragRelease();
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

// Track horizontal travel since press-down. If it exceeds the swipe cancel
// threshold we clear pressed state on the underlying object so its click
// handler does not fire on release — the press has clearly become a swipe.
// Issue #640: a swipe whose path crosses a button widget previously triggered
// the button on lift-up because LVGL routes touch events to the object under
// the press-down point regardless of finger movement.
void cancelClickIfSwiping(lv_indev_t *indev, lv_indev_state_t state) {
    static int16_t s_pressStartX = 0;
    static bool s_pressActive = false;
    static bool s_pressCancelled = false;

    if (state == LV_INDEV_STATE_RELEASED) {
        s_pressActive = false;
        s_pressCancelled = false;
        return;
    }

    lv_point_t p;
    lv_indev_get_point(indev, &p);

    if (!s_pressActive) {
        s_pressActive = true;
        s_pressCancelled = false;
        s_pressStartX = p.x;
        return;
    }

    if (s_pressCancelled)
        return;

    const int16_t travelX = static_cast<int16_t>(abs(p.x - s_pressStartX));
    if (travelX < SWIPE_CANCEL_THRESHOLD_PX)
        return;

    // Reset clears act_obj/last_pressed and sets reset_query so the in-flight
    // touch cycle terminates without dispatching LV_EVENT_CLICKED on release.
    // Also reset long-press timing so a stuck long-press timer can't refire.
    lv_indev_reset_long_press(indev);
    lv_indev_reset(indev, nullptr);
    s_pressCancelled = true;
    LOG_VDEBUG("UI", "Swipe cancelled pending click (travelX=%d)", travelX);
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
