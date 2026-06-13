
#ifndef CANSHIFT_FORMAT_FLOAT_RS_H
#define CANSHIFT_FORMAT_FLOAT_RS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t format_fixed_rs(char *buf, size_t size, float value, int32_t decimals);
size_t format_from_spec_rs(char *buf, size_t size, float value, const char *spec);
size_t format_general_rs(char *buf, size_t size, float value, int32_t sig_digits);

#ifdef __cplusplus
}
#endif

#endif
