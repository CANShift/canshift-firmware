// Keep in sync with rust/error-store/src/ffi.rs (#1177 R-5).
// All three symbols MUST be called inside portMUX critical section.
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

// Returns number written; does not mutate ring.
uint8_t error_store_get_all_rs(const FwError *ring, uint8_t ring_size, uint8_t head, uint8_t count,
                               FwError *out, uint8_t max_count);

// Out-of-range row is a no-op (and doesn't bump version).
void error_store_dismiss_at_rs(FwError *ring, uint8_t ring_size, uint8_t *head, uint8_t *count,
                               uint32_t *version, uint8_t row);

#ifdef __cplusplus
}
#endif

#endif // CANSHIFT_ERROR_STORE_RS_H
