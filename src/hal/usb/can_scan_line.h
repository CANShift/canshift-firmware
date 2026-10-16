#pragma once

#include <stddef.h>
#include <stdint.h>

namespace CanScanLine {

[[nodiscard]] size_t format(uint32_t id, uint8_t len, const uint8_t *data, char *out, size_t cap);

} // namespace CanScanLine
