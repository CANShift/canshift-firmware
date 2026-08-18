
#ifndef CANSHIFT_CAN_PARSER_RS_H
#define CANSHIFT_CAN_PARSER_RS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

inline constexpr int CANSHIFT_EXPR_MAX_TOKENS = 64;
inline constexpr int CANSHIFT_EXPR_MAX_REFS = 8;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t kind;
    uint8_t aux;
    float num;
} FfiTok;

typedef struct {
    uint16_t target_id;
    float value;
} RefValueRs;

float decode_bytes_rs(const uint8_t *data, uint8_t start_byte, uint8_t byte_len, bool big_endian,
                      bool is_signed, uint8_t bit_mask, float scale, float offset, size_t data_len);

int32_t lex_expr_rs(const uint8_t *expr, size_t expr_len, FfiTok *out, size_t out_cap);

float eval_tokens_rs(const uint8_t *data, uint8_t start_byte, uint8_t byte_len, bool big_endian,
                     bool is_signed, uint8_t bit_mask, float scale, float offset, size_t data_len,
                     const FfiTok *tokens, size_t n_tokens);

/* Returns NaN when the expression cannot be published: a reference the caller
   could not resolve, a parse failure, or a non-finite result. */
float eval_tokens_refs_rs(const uint8_t *data, uint8_t start_byte, uint8_t byte_len,
                          bool big_endian, bool is_signed, uint8_t bit_mask, float scale,
                          float offset, size_t data_len, const FfiTok *tokens, size_t n_tokens,
                          const RefValueRs *refs, size_t n_refs);

#ifdef __cplusplus
}

static_assert(sizeof(RefValueRs) == 8, "RefValueRs layout must match rust/can-parser RefValue");
#endif

#endif
