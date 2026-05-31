#pragma once
// page_manager.h — Dashboard page lifecycle and navigation
//
// The page manager owns all LVGL page objects.
// It creates pages from CfgPage config at boot, handles page switching,
// and drives per-page widget updates at render time.
//
// Page model:
//   - Each page is an lv_obj_t* (full-screen, 320x240 minus top bar)
//   - Pages are pre-created and hidden; navigation shows/hides them
//   - Page navigation triggers a brief transition animation
//   - The top bar is NOT part of any page — it is a persistent overlay

#include <lvgl.h>
#include <stdint.h>
#include <stdbool.h>
#include "config/config_types.h"

namespace PageManager {

/**
     * Initialize the page manager.
     * Creates all LVGL page objects from the loaded dashboard config.
     * Must be called after LVGL and ConfigLoader are initialized.
     */
void init();

/**
     * Navigate to a page by ID string.
     * Returns true if the page was found and navigation started.
     */
bool navigateTo(const char *pageId);

/**
     * Navigate to the next page (wraps around).
     */
void navigateNext();

/**
     * Navigate to the previous page (wraps around).
     */
void navigatePrev();

/**
     * Get the ID of the default (first shown) page from config.
     */
const char *getDefaultPageId();

/**
     * Update all widgets on the current page.
     * Call from the UI task at each render tick.
     * Reads from SignalStore and updates LVGL widget values.
     */
void updateWidgets();

/**
     * Apply rev limiter overlay to the current screen.
     * Shows/hides a semi-transparent red overlay object.
     * Called by AlertEngine via the UI task.
     */
void setRevLimiterOverlay(bool visible);

/**
 * Request a full page rebuild on the next updateWidgets() tick.
 * Called by ThemeManager::toggleDayMode() to re-apply the active theme.
 * Safe to call from LVGL event callbacks.
 */
void requestRebuild();

/**
 * Request a full config reload + page rebuild on the next updateWidgets()
 * tick. Called by USB CMD_PUT_CONFIG after the dashboard.json write
 * succeeds, replacing the previous esp_restart() path. Supersedes any
 * pending requestRebuild() — the reload also re-applies the theme.
 * Safe to call from non-UI tasks.
 */
void requestReload();

} // namespace PageManager
