#pragma once

#include <stdint.h>

namespace canshift_test {
inline uint32_t &mockMillisRef() {
    static uint32_t value = 0;
    return value;
}
}
