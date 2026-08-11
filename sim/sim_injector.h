#pragma once

#include <cstdint>

namespace SimInjector {

void init(const char *scenarioPath);

void tick(uint32_t nowMs);

} // namespace SimInjector
