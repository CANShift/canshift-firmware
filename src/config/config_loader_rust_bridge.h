#pragma once
// config_loader_rust_bridge.h — Header-only bridge selecting the
// `parseMajorVersion` backend (C++ vs Rust FFI) at compile time (#1207).
//
// Extracted from `config_loader.cpp` so the FFI stubs sit in one place
// instead of being sprinkled through the parser/validator translation units.
// The Rust port lives in `rust/config-loader/`; the C ABI is hand-written in
// `include/config_loader_rs.h`. See `rust/config-loader/src/ffi.rs` — both
// must move together in any PR that changes the bridge signature.
//
// Behaviour contract:
//   - Returns the major version (>= 0) on success.
//   - Returns -1 when the string is empty, missing, or not a parsable
//     integer. The -1 sentinel is load-bearing for `checkSchemaVersion`.
//   - The Rust shim tightens the contract (rejects leading whitespace and
//     `+`/`-` signs that C++ `strtol` accepted) and adds a bounded NUL scan
//     (MAX_VERSION_LEN = 32) so a missing terminator can't run off the
//     buffer. On the production wire — JSON-parsed canonical version strings
//     — both backends return identical results (#1177 R-10).

#include <climits>
#include <cstdlib>

#if USE_RUST_CONFIG_LOADER
    #include "config_loader_rs.h"
#endif

namespace ConfigLoaderInternal {

[[nodiscard]] inline int parseMajorVersion(const char *version) {
#if USE_RUST_CONFIG_LOADER
    return parse_major_version_rs(version);
#else
    if (!version || version[0] == '\0')
        return -1;
    char *end = nullptr;
    const long major = strtol(version, &end, 10);
    if (end == version || major < 0 || major > INT_MAX)
        return -1;
    return static_cast<int>(major);
#endif
}

} // namespace ConfigLoaderInternal
