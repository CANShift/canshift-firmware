#pragma once
// widget_factory.h — Widget instantiation and per-frame update dispatch
//
// The widget factory:
//   1. Creates LVGL widget objects from CfgWidget descriptors
//   2. Stores the binding between LVGL objects and signal IDs
//   3. Drives per-frame value updates by reading SignalStore and
//      updating the LVGL widget state

#include <lvgl.h>
#include "config/config_types.h"

namespace WidgetFactory {

/**
     * Create a widget on the given parent screen.
     * Returns the LVGL object, or nullptr on failure.
     *
     * @param parent    Parent lv_obj_t (page screen)
     * @param cfg       Widget config from dashboard.json
     * @param yOffset   Top-bar height offset applied to y positions
     */
lv_obj_t *create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset = 0);

/**
     * Update all widgets under the given parent screen.
     * Reads current signal values from SignalStore and updates LVGL objects.
     * Call from PageManager::updateWidgets() at each render tick.
     */
void updateAll(lv_obj_t *parent);

/**
     * Remove all registered widgets under a parent.
     * Call when navigating away from a page (if lazy creation is used).
     */
void clearAll(lv_obj_t *parent);

/**
     * Walk every widget registered under `parent` and ask each one to
     * re-apply theme-derived styles in place — no LVGL teardown, no widget
     * registry churn, no SPIFFS icon reloads.
     *
     * Issue #1257 — used by `PageManager::reapplyThemeAllPages()` as the
     * fast path for `ThemeManager::toggleDayMode()`. Heavy `rebuildAllPages`
     * stays the path for structural reloads (PUT_CONFIG → `requestReload`).
     */
void reapplyTheme(lv_obj_t *parent);

} // namespace WidgetFactory
