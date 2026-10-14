#pragma once

#include <stdint.h>

namespace LvglPool {

struct Stats {
    uint32_t total;
    uint32_t free;
    uint32_t largest;
};

[[nodiscard]] Stats stats();

void report(const char *phase);

} // namespace LvglPool
