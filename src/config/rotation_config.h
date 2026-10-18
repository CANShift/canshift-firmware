#pragma once

#include <stdint.h>

namespace RotationConfig {

uint16_t getOffsetDeg();

uint8_t computeLgfxRotation();

void applyAndReboot(uint16_t offsetDeg);

}
