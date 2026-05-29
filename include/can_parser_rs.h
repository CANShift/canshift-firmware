// can_parser_rs.h — C ABI for the Rust can-parser crate (issue #1177 R-1).
//
// Hand-written. The FFI surface is one function over primitive types;
// bindgen would drag libclang + a build.rs into the firmware build for no
// gain. Keep this header in sync with `rust/can-parser/src/ffi.rs` — both
// must move together in any PR that changes the bridge signature.
//
// `can_parser.cpp` consumes this header behind the existing C++
// `CanParser::detail::decodeBytes` interface when built with
// `USE_RUST_CAN_PARSER=1` so callers don't change.

#ifndef CANSHIFT_CAN_PARSER_RS_H
#define CANSHIFT_CAN_PARSER_RS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Resolve a CAN payload byte range to a decoded float. Returns 0.0f on:
//   - data == NULL
//   - byte_len == 0
//   - start_byte + byte_len > 8 (CAN_FRAME_MAX_BYTES)
//
// When `bit_mask != 0`, returns 1.0f if the masked bits in the packed raw
// value are set, 0.0f otherwise (ignores `is_signed` / `scale` / `offset`).
//
// The caller MUST pass `data` pointing to a readable buffer of at least
// 8 bytes. Mirrors `CanParser::detail::decodeBytes` byte-for-byte so the
// existing Unity suite (`test/test_can_parser/`) doubles as a parity gate.
float decode_bytes_rs(const uint8_t *data, uint8_t start_byte, uint8_t byte_len, bool big_endian,
                      bool is_signed, uint8_t bit_mask, float scale, float offset);

#ifdef __cplusplus
}
#endif

#endif // CANSHIFT_CAN_PARSER_RS_H
