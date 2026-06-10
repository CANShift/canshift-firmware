// Keep in sync with rust/sensor-color-ramp/src/ffi.rs (#1177 R-3).
#ifndef CANSHIFT_SENSOR_COLOR_RAMP_RS_H
#define CANSHIFT_SENSOR_COLOR_RAMP_RS_H

#include "config/config_types.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Returns 9 (Unknown) on null/empty/no-match.
uint8_t sensor_kind_from_name_rs(const char *name);

// Null ramp → 0x000000.
uint32_t color_at_value_rs(const CfgColorRampDef *ramp, float value);

// per_signal wins when count > 0; else default catalog; else null.
const CfgColorRampDef *resolve_ramp_rs(const CfgColorRampDef *per_signal, const char *signal_name);

// Same layout as kSensorDefaultRamps — used for byte-for-byte parity assert.
const CfgColorRampDef *sensor_default_ramps_rs(void);

uint8_t sensor_kind_count_rs(void);

#ifdef __cplusplus
}
#endif

#endif // CANSHIFT_SENSOR_COLOR_RAMP_RS_H
