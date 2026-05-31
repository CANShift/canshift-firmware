#pragma once
// sensor_color_ramp.h — value→color lookup for widget renderers (issue #430).
//
// Mirrors @tmbk/canshift-core's `ColorRamp` types and `SENSOR_DEFAULT_RAMPS`
// table. The defaults are validated at native-test time against the JSON
// fixture exported by `npm run export:sensor-defaults` so drift between TS and
// C++ is caught at CI rather than on the dashboard.

#include "config/config_types.h"

#include <stdint.h>

// Aliases — match the names used in this module while reusing the canonical
// JSON-parsed struct. Existing parser code in config_loader.cpp populates
// `CfgColorRampDef` directly; widget renderers and the default catalog refer
// to the alias so the two layers stay in sync.
using RampInterp = CfgRampInterp;
using CfgRampStop = CfgRampStopDef;
using CfgColorRamp = CfgColorRampDef;

// Built-in sensor catalog kinds — string discriminants in TypeScript.
enum class SensorKind : uint8_t {
    Coolant = 0,
    OilTemp,
    OilPress,
    BatteryVolts,
    Rpm,
    Afr,
    Boost,
    IntakeTemp,
    Egt,
    Unknown,
};

// Number of named sensor kinds (excludes Unknown). Indexes into
// kSensorDefaultRamps below.
constexpr uint8_t kSensorKindCount = 9;

// Default ramps shipped with the firmware — mirrored from
// canshift-core/src/sensorDefaults.ts. Anchored by the native test
// `test_sensor_color_ramp` against `test/fixtures/sensor_defaults.json`.
extern const CfgColorRamp kSensorDefaultRamps[kSensorKindCount];

// Map a free-form signal name (e.g. "coolant_temp_c") to a `SensorKind`.
// Mirrors `resolveSensorKind` in TypeScript. Case-insensitive, substring-based.
[[nodiscard]] SensorKind sensorKindFromName(const char *signalName);

// Resolve the active ramp for a widget. Prefers the per-signal ramp parsed
// from JSON; falls back to the sensor-name heuristic. Returns nullptr when
// neither resolves — caller keeps the legacy static color path.
[[nodiscard]] const CfgColorRamp *resolveRamp(const CfgColorRamp &perSignal,
                                              const char *signalName);

// Sample the ramp at `value`. Returns 0x00RRGGBB. O(count), no allocation.
// Below the first stop returns the first color; above the last returns the
// last. Empty ramps return 0x000000 (defensive — validator forbids them).
[[nodiscard]] uint32_t colorAtValue(const CfgColorRamp &ramp, float value);
