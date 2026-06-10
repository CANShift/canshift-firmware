// Keep in sync with src/ffi.rs (#1177 R-4).
#ifndef CANSHIFT_SIGNAL_MAP_RS_H
#define CANSHIFT_SIGNAL_MAP_RS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Returns SIGNAL_COUNT (64) on null/empty/too-long-without-NUL/unknown.
uint8_t signal_id_from_name_rs(const char *name);

#ifdef __cplusplus
}
#endif

#endif // CANSHIFT_SIGNAL_MAP_RS_H
