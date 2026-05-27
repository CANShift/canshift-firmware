// sw_sha256.h — software-only SHA-256 (FIPS 180-4).
//
// Why this exists: arduino-esp32 ships mbedTLS with the parallel_engine HW SHA
// backend. NimBLE acquires the HW SHA engine during BLE controller init at
// boot, so calling mbedtls_sha256_* from setup() while BLE is alive trips
// `sha_get_engine_state` assert (NULL engine) and panics. This bypass keeps
// SHA-256 available for non-perf-critical paths (e.g. boot-time HMAC key
// fingerprint logging) without touching the HW engine.
//
// Do NOT use this for OTA chunk verification — performance matters there,
// and during OTA the HW engine is free (WiFi/BLE are mutually exclusive at
// boot per #1152). Use mbedtls for those paths.

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace canshift {
namespace hal {
namespace wifi {

struct SwSha256Ctx {
    uint32_t state[8];
    uint64_t total_bytes;
    uint8_t buf[64];
    size_t buf_len;
};

void sw_sha256_init(SwSha256Ctx *ctx);
void sw_sha256_update(SwSha256Ctx *ctx, const uint8_t *data, size_t len);
void sw_sha256_final(SwSha256Ctx *ctx, uint8_t out[32]);

} // namespace wifi
} // namespace hal
} // namespace canshift
