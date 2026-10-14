#pragma once

#include <ArduinoJson.h>
#include <stddef.h>

namespace BleJsonBudget {

constexpr size_t kMaxWriteBytes = 256U;
constexpr size_t kPoolBytes = ARDUINOJSON_POOL_CAPACITY * 2 * sizeof(void *);
constexpr size_t kStringBytes = 512U;
constexpr size_t kStatusArenaBytes = kPoolBytes + kStringBytes;
constexpr size_t kCallbackArenaBytes = 2 * kPoolBytes;

} // namespace BleJsonBudget
