// config_loader_rs.h — C ABI for the Rust config-loader crate (issue #1177 R-10).
//
// Hand-written. The FFI surface is one function over a C string; bindgen
// would drag libclang + a build.rs into the firmware build for no gain.
// Keep this header in sync with `rust/config-loader/src/ffi.rs` — both must
// move together in any PR that changes the bridge signature.
//
// `config_loader.cpp` consumes this header behind the existing C++
// `parseMajorVersion` interface when built with `USE_RUST_CONFIG_LOADER=1`
// so the caller (`checkSchemaVersion`) doesn't change. `checkSchemaVersion`
// itself stays C++ because it is NOT pure — it logs and pushes to
// `ErrorStore`. Only the parse is moved.

#ifndef CANSHIFT_CONFIG_LOADER_RS_H
#define CANSHIFT_CONFIG_LOADER_RS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Extract the major component of a "major.minor.patch" version string.
// Returns the major version (>= 0) on success, or -1 on any failure:
//   - version pointer is NULL
//   - string is empty or has no leading digit
//   - integer overflow > INT32_MAX
//   - string lacks a NUL terminator within 32 bytes (defensive cap)
//
// Mirrors `parseMajorVersion` from `config_loader.cpp` — the -1 sentinel is
// load-bearing for the existing `checkSchemaVersion` caller.
int32_t parse_major_version_rs(const char *version);

#ifdef __cplusplus
}
#endif

#endif // CANSHIFT_CONFIG_LOADER_RS_H
