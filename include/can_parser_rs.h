// Keep in sync with rust/can-parser/src/ffi.rs (#1177 R-1).
#ifndef CANSHIFT_CAN_PARSER_RS_H
#define CANSHIFT_CAN_PARSER_RS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Returns 0.0f on null/empty/out-of-range. bit_mask != 0 → 1.0/0.0 (flag mode).
float decode_bytes_rs(const uint8_t *data, uint8_t start_byte, uint8_t byte_len, bool big_endian,
                      bool is_signed, uint8_t bit_mask, float scale, float offset);

#ifdef __cplusplus
}
#endif

#endif // CANSHIFT_CAN_PARSER_RS_H
