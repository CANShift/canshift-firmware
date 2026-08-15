#pragma once

#include <stdint.h>

namespace SimControls {

bool select(const char *scenario, uint32_t nowMs);

bool active();

void tick(uint32_t nowMs);

} // namespace SimControls
