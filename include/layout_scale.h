#pragma once

#include <stdint.h>

#include "board.h"
#include "layout_grid_rs.h"

namespace LayoutScale {

inline constexpr uint16_t kDesignWidth = 320;
inline constexpr uint16_t kDesignHeight = 240;

inline int16_t x(int16_t designValue) {
    if constexpr (canshift::display::kWidth == kDesignWidth) {
        return designValue;
    } else {
        return static_cast<int16_t>(
            layout_scale_rs(designValue, kDesignWidth, canshift::display::kWidth));
    }
}

inline int16_t y(int16_t designValue) {
    if constexpr (canshift::display::kHeight == kDesignHeight) {
        return designValue;
    } else {
        return static_cast<int16_t>(
            layout_scale_rs(designValue, kDesignHeight, canshift::display::kHeight));
    }
}

inline int16_t square(int16_t designValue) {
    const int16_t sx = x(designValue);
    const int16_t sy = y(designValue);
    return sx < sy ? sx : sy;
}

} // namespace LayoutScale
