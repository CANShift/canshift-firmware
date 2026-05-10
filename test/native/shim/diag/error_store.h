#pragma once
// diag/error_store.h shim — host tests don't surface UI errors. Stub the
// public API so config_loader.cpp links without dragging in the real ring
// buffer + portMUX implementation.

#include <stdint.h>

enum ErrorSource : uint8_t {
    ERROR_SRC_CAN = 0,
    ERROR_SRC_CONFIG = 1,
    ERROR_SRC_SYSTEM = 3,
};

struct FwError {
    ErrorSource source;
    char code[12];
    char message[52];
};

namespace ErrorStore {
inline void push(ErrorSource /*source*/, const char * /*code*/, const char * /*message*/) {}
inline void getAll(FwError * /*buf*/, uint8_t *count, uint8_t /*maxCount*/) {
    if (count)
        *count = 0;
}
inline uint8_t getCount() {
    return 0;
}
inline uint32_t getVersion() {
    return 0;
}
inline void dismissLatest() {}
inline void clear() {}
} // namespace ErrorStore
