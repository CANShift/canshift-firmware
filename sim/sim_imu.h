#pragma once

#include <stdint.h>

namespace SimImu {

void prepare(const char *scenario);

void tick(uint32_t nowMs);

}
