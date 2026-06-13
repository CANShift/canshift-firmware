
#ifndef CANSHIFT_SENSOR_COLOR_RAMP_RS_H
#define CANSHIFT_SENSOR_COLOR_RAMP_RS_H

#include "config/config_types.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t sensor_kind_from_name_rs(const char *name);

uint32_t color_at_value_rs(const CfgColorRampDef *ramp, float value);

const CfgColorRampDef *resolve_ramp_rs(const CfgColorRampDef *per_signal, const char *signal_name);

const CfgColorRampDef *sensor_default_ramps_rs(void);

uint8_t sensor_kind_count_rs(void);

#ifdef __cplusplus
}
#endif

#endif
