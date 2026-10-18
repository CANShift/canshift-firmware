
#ifndef CANSHIFT_SIGNAL_MAP_RS_H
#define CANSHIFT_SIGNAL_MAP_RS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t signal_id_from_name_rs(const char *name);
float signal_display_step_rs(uint8_t id, float previous, float sample, uint32_t dt_ms, bool seeded);

#ifdef __cplusplus
}
#endif

#endif
