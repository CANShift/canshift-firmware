
#ifndef CANSHIFT_CAN_PARSER_RS_H
#define CANSHIFT_CAN_PARSER_RS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CANSHIFT_EXPR_MAX_TOKENS 64

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t kind;
    uint8_t aux;
    float num;
} FfiTok;

float decode_bytes_rs(const uint8_t *data, uint8_t start_byte, uint8_t byte_len, bool big_endian,
                      bool is_signed, uint8_t bit_mask, float scale, float offset, size_t data_len);

int32_t lex_expr_rs(const uint8_t *expr, size_t expr_len, FfiTok *out, size_t out_cap);

float eval_tokens_rs(const uint8_t *data, uint8_t start_byte, uint8_t byte_len, bool big_endian,
                     bool is_signed, uint8_t bit_mask, float scale, float offset, size_t data_len,
                     const FfiTok *tokens, size_t n_tokens);

#ifdef __cplusplus
}
#endif

#endif
