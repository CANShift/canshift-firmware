#pragma once

#include <stdint.h>

namespace BusHealth {

struct State {
    bool silent;
    uint32_t seconds;
};

[[nodiscard]] State sample();

} // namespace BusHealth
