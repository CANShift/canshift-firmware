#pragma once

#include <stdint.h>

namespace SimOverlays {

void start(const char *scenario, uint32_t nowMs);

void toggleOtaDemo(uint32_t nowMs);

void tick(uint32_t nowMs);

}
