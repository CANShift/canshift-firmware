
#ifndef CANSHIFT_ERROR_STORE_RS_H
#define CANSHIFT_ERROR_STORE_RS_H

#include <stdint.h>

#ifdef __cplusplus
struct FwError;
extern "C" {
#else
struct FwError;
typedef struct FwError FwError;
#endif

void error_store_push_rs(FwError *ring, uint8_t ring_size, uint8_t *head, uint8_t *count,
                         uint32_t *version, uint8_t source, const char *code, const char *message);

uint8_t error_store_get_all_rs(const FwError *ring, uint8_t ring_size, uint8_t head, uint8_t count,
                               FwError *out, uint8_t max_count);

void error_store_dismiss_at_rs(FwError *ring, uint8_t ring_size, uint8_t *head, uint8_t *count,
                               uint32_t *version, uint8_t row);

#ifdef __cplusplus
}
#endif

#endif
