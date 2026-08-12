#pragma once

#include <stdint.h>

namespace DashMetrics {

constexpr int16_t kFramePaddingPx = 8;
constexpr int16_t kRowGapPx = 6;
constexpr int16_t kShiftStripHeightPx = 7;
constexpr int16_t kShiftStripGapPx = 2;

constexpr int16_t kShiftStripBandPx = kShiftStripHeightPx + kRowGapPx;

} // namespace DashMetrics
