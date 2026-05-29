// format_float_rs.h — C ABI for the Rust format-float crate (issue #1177 R-2).
//
// Hand-written. The FFI surface is three stateless functions over primitives;
// bindgen would drag libclang + a build.rs into the firmware build for no
// gain. Keep this header in sync with `rust/format-float/src/ffi.rs` — both
// must move together in any PR that changes the bridge signatures.
//
// `format_float.cpp` consumes this header behind the existing C++
// `FloatFormat::formatFixed` / `formatFromSpec` / `formatGeneral` interface
// when built with `USE_RUST_FORMAT_FLOAT=1` so the four production callers
// (top bar, widget helpers, BLE server, USB telemetry) don't change.

#ifndef CANSHIFT_FORMAT_FLOAT_RS_H
#define CANSHIFT_FORMAT_FLOAT_RS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Format `value` with `decimals` digits after the point into `buf`. Mirrors
// `FloatFormat::formatFixed` byte-for-byte (the new `test/test_format_float/`
// Unity suite is the parity gate). Returns the snprintf-style would-have-
// written length.
size_t format_fixed_rs(char *buf, size_t size, float value, int32_t decimals);

// Render `value` into `buf` according to a printf-style format string of the
// form `"<prefix>%[.N]f<suffix>"`. On any unrecognised format: ignores the
// spec and renders a plain `%.1f` (preserves intent for legacy configs).
size_t format_from_spec_rs(char *buf, size_t size, float value, const char *spec);

// Format `value` like `"%.<sig_digits>g"` — fixed notation, trailing-zero
// strip, JSON-safe output. Used by the USB telemetry payload.
size_t format_general_rs(char *buf, size_t size, float value, int32_t sig_digits);

#ifdef __cplusplus
}
#endif

#endif // CANSHIFT_FORMAT_FLOAT_RS_H
