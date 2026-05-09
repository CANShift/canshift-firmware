#pragma once
// gauge_widget.h — Arc-based gauge widget (RPM, boost, temperatures, etc.)
//
// Renders using LVGL's lv_arc widget. Shows:
//   - Value arc (colored based on value level)
//   - Background arc track
//   - Numeric value label in center
//
// Design note for 320x240 display:
//   - Main gauge (RPM): 160x140 region — use 140px diameter arc
//   - Secondary gauges: 80x80 regions — use 70px diameter arc
//   - Keep font size ≥ 20 for glanceability at speed

#include <lvgl.h>
#include "config/config_types.h"

namespace GaugeWidget {

/**
     * Create a gauge widget on the parent object.
     * Returns the LVGL container object.
     */
lv_obj_t *create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset);

/**
     * Update the gauge with a new value.
     *
     * @param obj    The gauge container returned by create()
     * @param value  Current signal value (smoothed)
     * @param valid  False if signal timed out — shows "---"
     * @param cfg    Widget config (for min/max/thresholds)
     */
void update(lv_obj_t *obj, float value, bool valid, const CfgWidget &cfg);

} // namespace GaugeWidget
