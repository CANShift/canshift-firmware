#pragma once

#include <stdint.h>

#include "board.h"
#include "display_tiers.h"
#include "layout_grid_rs.h"

namespace LayoutScale {

inline constexpr uint16_t kDesignWidth = canshift::display::kBaseTier.designWidth;
inline constexpr uint16_t kDesignHeight = canshift::display::kBaseTier.designHeight;

inline int16_t x(int16_t designValue) {
    return static_cast<int16_t>(
        layout_scale_rs(designValue, kDesignWidth, canshift::display::width()));
}

inline int16_t y(int16_t designValue) {
    return static_cast<int16_t>(
        layout_scale_rs(designValue, kDesignHeight, canshift::display::height()));
}

inline int16_t square(int16_t designValue) {
    const int16_t sx = x(designValue);
    const int16_t sy = y(designValue);
    return sx < sy ? sx : sy;
}

} // namespace LayoutScale
