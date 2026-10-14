#pragma once

#include <cstdint>

namespace PerfTimer {

using Op = void (*)(uint32_t index);

struct Stats {
    double medianNs;
    double p25Ns;
    double p75Ns;
    uint32_t iterations;
    uint32_t samples;
};

[[nodiscard]] Stats measure(Op op, uint32_t samples);

void keep(uint64_t value);

} // namespace PerfTimer
