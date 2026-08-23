
#ifndef CANSHIFT_UNIT_CONVERT_RS_H
#define CANSHIFT_UNIT_CONVERT_RS_H

#include <stdint.h>

#define UNIT_SYSTEM_METRIC 0
#define UNIT_SYSTEM_IMPERIAL 1

#ifdef __cplusplus
extern "C" {
#endif

const char *unit_display_symbol_rs(const char *unit, uint8_t system);
float unit_display_value_rs(float value, const char *unit, uint8_t system);

#ifdef __cplusplus
}
#endif

#endif
