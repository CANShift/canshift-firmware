#pragma once

#include <stdint.h>

namespace SimSplash {

bool select(const char *scenario, uint32_t nowMs);

bool active();

void tick(uint32_t nowMs);

} // namespace SimSplash
