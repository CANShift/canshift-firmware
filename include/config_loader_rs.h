// Keep in sync with rust/config-loader/src/ffi.rs (#1177 R-10).
#ifndef CANSHIFT_CONFIG_LOADER_RS_H
#define CANSHIFT_CONFIG_LOADER_RS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Returns -1 on null/empty/overflow/no-NUL within 32 bytes.
int32_t parse_major_version_rs(const char *version);

#ifdef __cplusplus
}
#endif

#endif // CANSHIFT_CONFIG_LOADER_RS_H
