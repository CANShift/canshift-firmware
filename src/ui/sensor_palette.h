#pragma once

#include <stdint.h>

struct SensorPaletteEntry {
    const char *iconName;
    uint32_t okColor;
    uint32_t warningColor;
};

namespace SensorPalette {

constexpr uint32_t kSentinelNoWarning = 0u;

[[nodiscard]] const SensorPaletteEntry *lookup(const char *iconName);

[[nodiscard]] uint32_t fillColor(const char *iconName, float value, float dangerLevel);

} // namespace SensorPalette
