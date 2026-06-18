
#ifndef CANSHIFT_CAN_PARSER_RS_H
#define CANSHIFT_CAN_PARSER_RS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

float decode_bytes_rs(const uint8_t *data, uint8_t start_byte, uint8_t byte_len, bool big_endian,
                      bool is_signed, uint8_t bit_mask, float scale, float offset);

float decode_with_expr_rs(const uint8_t *data, uint8_t start_byte, uint8_t byte_len,
                          bool big_endian, bool is_signed, uint8_t bit_mask, float scale,
                          float offset, const uint8_t *expr, size_t expr_len);

#ifdef __cplusplus
}
#endif

#endif
