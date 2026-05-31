// sensor_color_ramp_rs.h — C ABI for the Rust sensor-color-ramp crate
// (issue #1177 R-3, firmware-only phase).
//
// Hand-written. The FFI surface is three stateless functions over the
// existing `CfgColorRampDef` aggregate; bindgen would drag libclang + a
// build.rs into the firmware build for no gain. Keep this header in sync
// with `rust/sensor-color-ramp/src/ffi.rs` — both must move together in any
// PR that changes a signature.
//
// `sensor_color_ramp.cpp` consumes this header behind the existing C++
// surface (`sensorKindFromName`, `colorAtValue`, `resolveRamp`) when built
// with `USE_RUST_SENSOR_COLOR_RAMP=1` so widget callers don't change.
//
// The Rust `ColorRamp` mirror is `#[repr(C)]` and identical to
// `CfgColorRampDef`. A compile-time `assert!` on `size_of::<ColorRamp>()`
// guards against drift if anyone bumps `CFG_MAX_RAMP_STOPS` without
// updating the Rust constant.

#ifndef CANSHIFT_SENSOR_COLOR_RAMP_RS_H
#define CANSHIFT_SENSOR_COLOR_RAMP_RS_H

#include "config/config_types.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Map a free-form signal name (NUL-terminated C string) to a `SensorKind`
// discriminant. Returns 9 (`SensorKind::Unknown`) on NULL / empty / no-match
// inputs. Caller casts to the C++ enum.
uint8_t sensor_kind_from_name_rs(const char *name);

// Sample the ramp at `value`. Returns `0x00RRGGBB`. NULL `ramp` returns
// `0x000000` (defensive — the C++ caller already checks).
uint32_t color_at_value_rs(const CfgColorRampDef *ramp, float value);

// Resolve the active ramp for a widget. Returns either `per_signal` (when
// its `count > 0`), a pointer into the Rust static default catalog (when
// the name resolves), or NULL.
const CfgColorRampDef *resolve_ramp_rs(const CfgColorRampDef *per_signal, const char *signal_name);

// Static default catalog from the Rust side — same layout as the C++
// `kSensorDefaultRamps` array. Used to assert byte-for-byte parity at boot
// during the delegation flip.
const CfgColorRampDef *sensor_default_ramps_rs(void);

// Catalog length — must equal `kSensorKindCount` in the C++ header (9).
uint8_t sensor_kind_count_rs(void);

#ifdef __cplusplus
}
#endif

#endif // CANSHIFT_SENSOR_COLOR_RAMP_RS_H
