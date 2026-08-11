#pragma once

#include <lvgl.h>
#include <stdint.h>

namespace CruiseLShape {

inline constexpr uint8_t kMaxPoints = 40;

enum class Corner : uint8_t { kTL = 0, kTR = 1, kBL = 2, kBR = 3 };

struct Geometry {
    int16_t radius;
    int16_t innerRadius;
    int16_t notchW;
    int16_t notchH;
};

uint8_t buildOutline(lv_point_t *pts, const lv_area_t &area, Corner corner, const Geometry &geom);

void buildFillArms(const lv_area_t &btn, Corner corner, const Geometry &geom, lv_area_t *armH,
                   lv_area_t *armV);

bool hitInNotch(const lv_area_t &area, Corner corner, const Geometry &geom,
                const lv_point_t &point);

} // namespace CruiseLShape
