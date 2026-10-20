#pragma once

#include <stddef.h>
#include <stdint.h>

namespace CanMonitor {

[[nodiscard]] bool start();

void stop();

[[nodiscard]] bool active();

void offer(uint32_t id, bool extended, const uint8_t *data, uint8_t len, uint32_t nowMs);

[[nodiscard]] bool setFilter(const uint8_t *data, size_t len);

[[nodiscard]] size_t encode(uint32_t nowMs, uint8_t *out, size_t cap);

}
