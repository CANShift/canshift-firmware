#pragma once
// setup_screen.h — Standalone "no dashboard.json" landing screen.
//
// Shown by PageManager::init() when ConfigLoader reports no loaded dashboard.
// Pure rendering — no state, no callbacks. Extracted from page_manager.cpp
// to keep the page-lifecycle TU focused on lifecycle (issue #704).

namespace SetupScreen {

/**
 * Build and load the setup landing screen on the LVGL active screen.
 * Must be called on the UI task while the LVGL mutex is held.
 */
void show();

} // namespace SetupScreen
