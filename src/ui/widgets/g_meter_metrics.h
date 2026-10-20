#pragma once

#include <stdint.h>

namespace GMeterMetrics {

constexpr int16_t kPlotTopInsetPx = 4;
constexpr int16_t kPlotBottomInsetPx = 3;
constexpr int16_t kPlotRightInsetPx = 8;
constexpr int16_t kReadoutColumnPx = 52;
constexpr int16_t kReadoutTopPx = 16;
constexpr int16_t kDotSidePx = 6;
constexpr int16_t kStrokePx = 1;
constexpr uint8_t kValueFontPx = 17;
constexpr uint8_t kValueDecimals = 2;
constexpr int16_t kInnerRingDivisor = 2;

struct PlotBox {
    int16_t x;
    int16_t y;
    int16_t side;
};

inline PlotBox plotBox(int16_t boxW, int16_t boxH) {
    const int16_t byHeight = static_cast<int16_t>(boxH - kPlotTopInsetPx - kPlotBottomInsetPx);
    const int16_t byWidth = static_cast<int16_t>(boxW - kPlotRightInsetPx - kReadoutColumnPx);
    const int16_t fit = byHeight < byWidth ? byHeight : byWidth;
    const int16_t side = fit > 0 ? fit : 0;
    return {static_cast<int16_t>(boxW - kPlotRightInsetPx - side), kPlotTopInsetPx, side};
}

}
