#pragma once

#include <stdint.h>

namespace ScreenProfile {

struct DesignDimensions {
    uint16_t width;
    uint16_t height;
};

struct ScaleFactors {
    float x;
    float y;
};

DesignDimensions lookupDesignDimensions(const char *profileId);

void initFromDashboard();

ScaleFactors getScaleFactors();

int16_t scaleXVal(int16_t value);
int16_t scaleYVal(int16_t value);

}
