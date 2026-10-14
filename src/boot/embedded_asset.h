#pragma once

#include <stddef.h>
#include <stdint.h>

namespace EmbeddedAsset {

enum class Outcome : uint8_t {
    Written,
    Empty,
    NoMemory,
    Corrupt,
    WriteFailed,
};

constexpr size_t kOutcomeCount = static_cast<size_t>(Outcome::WriteFailed) + 1;

[[nodiscard]] bool contentStamp(const uint8_t *start, const uint8_t *end, uint32_t &stamp);

Outcome inflateToFile(const char *path, const uint8_t *start, const uint8_t *end,
                      size_t &inflatedLength);

} // namespace EmbeddedAsset
