#pragma once
// bar_widget.h — Horizontal / vertical progress bar widget
//
// Uses LVGL lv_bar. Orientation is determined by the widget dimensions:
//   - width >= height → horizontal (left-to-right fill)
//   - height  > width → vertical   (bottom-to-top fill)
//
// Indicator color changes based on warning/danger thresholds from CfgBarParams.
// Suitable for full-width TPS strips, vertical RPM bars, etc.

#include <lvgl.h>
#include "config/config_types.h"

namespace BarWidget {

/**
     * Create a bar widget on the parent object.
     * Returns the LVGL container object.
     */
lv_obj_t *create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset);

/**
     * Update the bar with a new signal value.
     *
     * @param obj    The container returned by create()
     * @param value  Current signal value
     * @param valid  False if signal timed out — bar resets to min
     * @param cfg    Widget config (for bar thresholds and colors)
     */
void update(lv_obj_t *obj, float value, bool valid, const CfgWidget &cfg);

} // namespace BarWidget
