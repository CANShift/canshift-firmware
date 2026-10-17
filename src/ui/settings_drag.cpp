#include "ui/settings_drag.h"

#include "diag/logger.h"
#include "ui/settings_page.h"
#include "ui/top_bar.h"

#include <stdlib.h>

namespace SettingsDrag {

namespace {

constexpr int16_t kDragHotzoneMaxPx = 40;

constexpr int16_t kDragStartThresholdPx = 6;

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

void releaseDrag(lv_indev_t *indev) {
    if (!s_drag.active)
        return;
    const bool wasTracking = s_drag.tracking;
    onDragRelease();
    if (!wasTracking)
        return;
    lv_indev_reset_long_press(indev);
    lv_indev_reset(indev, nullptr);
}

void armDrag(int16_t y) {
    s_drag.active = true;
    s_drag.tracking = false;
    s_drag.startY = y;
    s_dragCrossedThreshold = false;

    if (!inHotzone(y)) {
        s_drag.active = false;
        return;
    }
    s_drag.startPanelY =
        SettingsPage::isOpen() ? SettingsPage::getOpenY() : SettingsPage::getClosedY();
}

bool startTracking(int16_t deltaY) {
    if (abs(deltaY) < kDragStartThresholdPx)
        return false;

    const bool isOpen = SettingsPage::isOpen();
    if (!isOpen && deltaY <= 0)
        return false;
    if (isOpen && deltaY >= 0)
        return false;

    s_drag.tracking = true;
    SettingsPage::setDragging(true);
    LOG_VDEBUG("UI", "Drag: settings tracker armed (open=%d)", static_cast<int>(isOpen));
    return true;
}

void followFinger(int16_t deltaY) {
    int16_t targetY = static_cast<int16_t>(s_drag.startPanelY + deltaY);
    SettingsPage::setPanelY(targetY);

    const int16_t closedY = SettingsPage::getClosedY();
    const int16_t openY = SettingsPage::getOpenY();
    const int16_t midpointY = closedY + (openY - closedY) / 2;
    const bool startedClosed = (s_drag.startPanelY == closedY);
    s_dragCrossedThreshold = startedClosed ? (targetY >= midpointY) : (targetY <= midpointY);
}

} // namespace

void update(lv_indev_t *indev, lv_indev_state_t state) {
    if (state == LV_INDEV_STATE_RELEASED) {
        releaseDrag(indev);
        return;
    }

    lv_point_t p;
    lv_indev_get_point(indev, &p);

    if (!s_drag.active)
        armDrag(p.y);
    if (!s_drag.active)
        return;

    const int16_t deltaY = p.y - s_drag.startY;
    if (!s_drag.tracking && !startTracking(deltaY))
        return;
    followFinger(deltaY);
}

bool inHotzone(int16_t y) {
    return y <= LV_MIN(kDragHotzoneMaxPx, TopBar::getHeight());
}

} // namespace SettingsDrag
