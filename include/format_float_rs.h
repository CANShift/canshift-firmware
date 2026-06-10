// Keep in sync with rust/format-float/src/ffi.rs (#1177 R-2).
#ifndef CANSHIFT_FORMAT_FLOAT_RS_H
#define CANSHIFT_FORMAT_FLOAT_RS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// All three return snprintf-style would-have-written length.
size_t format_fixed_rs(char *buf, size_t size, float value, int32_t decimals);
size_t format_from_spec_rs(char *buf, size_t size, float value, const char *spec);
size_t format_general_rs(char *buf, size_t size, float value, int32_t sig_digits);

#ifdef __cplusplus
}
#endif

#endif // CANSHIFT_FORMAT_FLOAT_RS_H
