#pragma once

#include "ui/widgets/cruise_l_shape.h"

#include <lvgl.h>
#include <stdint.h>

namespace CruiseLButton {

inline constexpr int16_t kReadoutW = 100;
inline constexpr int16_t kReadoutH = 76;
inline constexpr uint32_t kFillRgb = 0x1A1A1Au;
inline constexpr uint32_t kTextRgb = 0xFFFFFFu;

CruiseLShape::Geometry geometry();

void initShell(lv_obj_t *btn, int16_t x, int16_t y, int16_t w, int16_t h);

void attach(lv_obj_t *btn, CruiseLShape::Corner corner);

}
