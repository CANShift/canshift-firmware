#pragma once

#include "config/config_types.h"

#include <lvgl.h>
#include <stdint.h>

namespace GaugeArcs {

constexpr int16_t kArcYShift = 2;

uint8_t strokeWidthFor(const CfgWidget &cfg);
uint16_t valueToAngle(float value, float minVal, float maxVal);
int32_t diameterFor(const CfgWidget &cfg);
void buildBackgroundTrack(lv_obj_t *cont, int32_t diam, uint8_t strokeW);
lv_obj_t *buildValueFill(lv_obj_t *cont, int32_t diam, uint32_t inkRgb, uint8_t strokeW);
void angleAnimCb(void *obj, int32_t angle);

}
