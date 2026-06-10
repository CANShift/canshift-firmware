// Table mirrors SENSOR_PALETTE in canshift-core/src/sensor-palette.ts.
#include "sensor_palette.h"

#include <cmath>
#include <string.h>

namespace {

constexpr uint32_t RED = 0xCC3333u;
constexpr uint32_t AMBER = 0xFFA000u;

// Order matches the SensorIconName Zod enum — drift caught by JSON-fixture test.
constexpr SensorPaletteEntry kEntries[] = {
    {"rpm", 0x00ACC1u, RED},
    {"speed", 0xECEFF1u, SensorPalette::kSentinelNoWarning},
    {"coolant", 0x1E88E5u, RED},
    {"oil_pressure", 0x4CAF50u, RED},
    {"oil_temp", 0xF5A623u, RED},
    {"battery", 0xFBC02Du, RED},
    {"fuel", 0x4CAF50u, AMBER},
    {"afr", 0xC2185Bu, RED},
    {"boost", 0x8E24AAu, RED},
    {"throttle", 0xFB8C00u, SensorPalette::kSentinelNoWarning},
    {"iat", 0x4FC3F7u, RED},
    {"gear", 0xECEFF1u, SensorPalette::kSentinelNoWarning},
    {"timer", 0xECEFF1u, SensorPalette::kSentinelNoWarning},
    {"warning", RED, SensorPalette::kSentinelNoWarning},
    {"flame", 0xFF6F00u, RED},
    {"turbo", 0x8E24AAu, RED},
    {"engine", 0x4CAF50u, RED},
    {"brake", RED, SensorPalette::kSentinelNoWarning},
    {"launch", 0x43A047u, SensorPalette::kSentinelNoWarning},
    {"traction", 0x43A047u, SensorPalette::kSentinelNoWarning},
    {"map_icon", 0x42A5F5u, SensorPalette::kSentinelNoWarning},
    {"exhaust", 0xFB8C00u, RED},
    {"cog", 0x9E9E9Eu, SensorPalette::kSentinelNoWarning},
};

} // namespace

const SensorPaletteEntry *SensorPalette::lookup(const char *iconName) {
    if (!iconName || iconName[0] == '\0')
        return nullptr;
    for (const SensorPaletteEntry &entry : kEntries) {
        if (strcmp(entry.iconName, iconName) == 0)
            return &entry;
    }
    return nullptr;
}

uint32_t SensorPalette::fillColor(const char *iconName, float value, float dangerLevel) {
    const SensorPaletteEntry *entry = lookup(iconName);
    if (!entry)
        return 0u;
    if (entry->warningColor == kSentinelNoWarning)
        return entry->okColor;
    if (std::isnan(dangerLevel) || value < dangerLevel)
        return entry->okColor;
    return entry->warningColor;
}
