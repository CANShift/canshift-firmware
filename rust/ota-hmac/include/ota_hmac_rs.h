// ota_hmac_rs.h — C ABI for the Rust ota_hmac crate (issue #827 Phase 2).
//
// Hand-written, on purpose. The FFI surface is 6 functions of primitive
// types; bindgen would drag libclang + a build.rs into the firmware build
// for no gain. Keep this header in sync with `src/ffi.rs` — both must move
// together in any PR that changes the bridge signature.
//
// Phase 3's `src/hal/wifi/ota_hmac_bridge.cpp` consumes this header and
// wraps these functions behind the existing C++ `OtaHmac::OtaHmacVerifier`
// interface so callers don't change.

#ifndef CANSHIFT_OTA_HMAC_RS_H
#define CANSHIFT_OTA_HMAC_RS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque verifier state. Storage owned by the caller — see
// `ota_hmac_rs_sizeof()` / `ota_hmac_rs_alignof()` for the required buffer
// dimensions. Treat any reads outside the documented API as UB.
typedef struct OtaHmacRs OtaHmacRs;

// Sink callback signature. Returns 0 on success, non-zero to abort the
// stream. `user` is forwarded unchanged from `ota_hmac_rs_init`.
typedef int32_t (*OtaHmacRsSinkFn)(const uint8_t *data, size_t len, void *user);

// Storage requirements — call once at static-assert time.
size_t ota_hmac_rs_sizeof(void);
size_t ota_hmac_rs_alignof(void);

// Initialise a zero-filled `slot`. Returns 0 on success; non-zero codes:
//   1 — slot or sink_cb null
//   2 — secret_len exceeds 64 (MAX_SECRET_LEN)
//   3 — secret null but secret_len > 0
//   4 — HMAC backend rejected the secret
int32_t ota_hmac_rs_init(OtaHmacRs *slot, const uint8_t *secret, size_t secret_len,
                         OtaHmacRsSinkFn sink_cb, void *sink_user);

// Call once before any feed. Returns 0 on success.
int32_t ota_hmac_rs_begin(OtaHmacRs *slot);

// Stream a chunk. Returns 0 on success, non-zero on failure (sink rejected
// the data, backend update failed, or the verifier is in a failed state).
int32_t ota_hmac_rs_feed(OtaHmacRs *slot, const uint8_t *data, size_t len);

// Finalise. Returns 0 iff the trailing 32 bytes matched the computed HMAC.
int32_t ota_hmac_rs_finish(OtaHmacRs *slot);

// Total bytes accepted via feed (body + trailer). 0 when slot is null or
// uninitialised.
size_t ota_hmac_rs_total_bytes(const OtaHmacRs *slot);

// Release the verifier. The slot can be re-initialised after this.
void ota_hmac_rs_destroy(OtaHmacRs *slot);

// Constant-time memcmp (exposed so callers can use it independently of the
// verifier). Returns 0 iff equal over `len` bytes.
int32_t ota_hmac_rs_const_memcmp(const uint8_t *a, const uint8_t *b, size_t len);

#ifdef __cplusplus
}
#endif

#endif // CANSHIFT_OTA_HMAC_RS_H
