// signal_map_rs.h — C ABI for the Rust signal-map crate (issue #1177 R-4).
//
// Hand-written. The FFI surface is one function over primitive types;
// bindgen would drag libclang + a build.rs into the firmware build for no
// gain. Keep this header in sync with `src/ffi.rs` — both must move
// together in any PR that changes the bridge signature.
//
// `signal_map.cpp` consumes this header behind the existing C++
// `signalIdFromName(const char *)` interface when built with
// `USE_RUST_SIGNAL_MAP=1` so callers don't change.

#ifndef CANSHIFT_SIGNAL_MAP_RS_H
#define CANSHIFT_SIGNAL_MAP_RS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Resolve a NUL-terminated C string to a SignalId. Returns
// `SIGNAL_COUNT` (64) on any of:
//   - name == NULL
//   - name is the empty string
//   - name has no NUL within 32 bytes (defensive — guards against a
//     missing terminator the C++ original assumed away)
//   - name is not in the known signal table
//
// The caller MUST pass a readable, NUL-terminated C string. The
// function never writes to caller memory.
uint8_t signal_id_from_name_rs(const char *name);

#ifdef __cplusplus
}
#endif

#endif // CANSHIFT_SIGNAL_MAP_RS_H
