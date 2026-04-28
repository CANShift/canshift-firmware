#pragma once
// top_bar.h — Persistent top status bar
//
// The top bar is a fixed overlay (lv_layer_top()) that persists across page changes.
// It shows:
//   - Map name / profile from the ECU (SignalIds::MAP_NUMBER, MAP_NAME_IDX)
//   - Optional status icons (MIL, launch control, etc.)
//   - Optional page indicator dots
//
// Height is configured in CfgTopBar (default 24px).

#include <lvgl.h>

namespace TopBar {

    /**
     * Create the top bar on lv_layer_top().
     * Call once from PageManager::init().
     */
    void init();

    /**
     * Update the top bar content (map name, icons).
     * Call from the UI task at a reduced rate (e.g. every 500ms).
     */
    void update();

    /**
     * Return the top bar height in pixels.
     * Used by page manager to offset widget y positions.
     */
    int16_t getHeight();

} // namespace TopBar
