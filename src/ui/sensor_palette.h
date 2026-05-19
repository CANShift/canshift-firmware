#pragma once
// sensor_palette.h — semantic per-sensor two-zone colour palette (issue #954).
//
// Mirrors `SENSOR_PALETTE` in canshift-core/src/sensor-palette.ts. Each
// `SensorIconName` resolves to an "OK zone" colour matching the metric (water
// blue for coolant, violet for boost, ...) plus an optional warning colour
// used above `dangerLevel`. Gauges and bars look up by the string iconName
// stored in `CfgGaugeParams::iconName` / `CfgBarParams::iconName`.
//
// Drift against the TS source is caught at native-test time by
// test_sensor_palette against the fixture exported by
// `npm run export:sensor-palette` in canshift-core.

#include <stdint.h>

struct SensorPaletteEntry {
    const char *iconName;  // matches a SensorIconName string in canshift-core
    uint32_t okColor;      // opaque fill below dangerLevel (0xRRGGBB)
    uint32_t warningColor; // fill above dangerLevel; 0 = no semantic warning
};

namespace SensorPalette {

// kSentinelNoWarning — flag value stored in `warningColor` when a sensor has
// no semantic upper warning (throttle, speed). Callers keep the OK colour
// across the full range in that case. Chosen as 0 because no palette entry
// uses pure black; `lookup()` returns the pointer only when found.
constexpr uint32_t kSentinelNoWarning = 0u;

// Returns the entry for a given iconName, or nullptr when the name is empty
// or doesn't match a known sensor. Case-sensitive — values come from the
// Zod enum so the casing is fixed.
const SensorPaletteEntry *lookup(const char *iconName);

// Resolve the fill colour for a value relative to the danger threshold. When
// no palette entry matches, returns 0 — callers fall back to their existing
// per-widget colour path (style.primaryColor or legacy zone tints).
//
// Caller-supplied semantics: `value` and `dangerLevel` are in the same
// native unit. NaN `dangerLevel` is treated as "no threshold" so the OK
// colour fills the entire range.
uint32_t fillColor(const char *iconName, float value, float dangerLevel);

} // namespace SensorPalette
