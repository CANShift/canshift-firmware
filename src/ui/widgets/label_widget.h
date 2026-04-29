#pragma once
// label_widget.h — Text value label widget
// Shows a signal value as formatted text with optional prefix/suffix.
// Used for speed, temperature readouts, gear indicator, AFR, etc.

#include <lvgl.h>
#include "config/config_types.h"

namespace LabelWidget {
lv_obj_t *create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset);
void update(lv_obj_t *obj, float value, bool valid, const CfgWidget &cfg);
} // namespace LabelWidget
