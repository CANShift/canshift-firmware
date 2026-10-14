#pragma once

extern "C" {
#include <sha256.h>
}

#include <cstddef>
#include <cstdint>

using mbedtls_sha256_context = SHA256_CTX;

inline void mbedtls_sha256_init(mbedtls_sha256_context *ctx) {
    sha256_init(ctx);
}

inline void mbedtls_sha256_free(mbedtls_sha256_context *) {}

inline int mbedtls_sha256_starts_ret(mbedtls_sha256_context *ctx, int) {
    sha256_init(ctx);
    return 0;
}

inline int mbedtls_sha256_update_ret(mbedtls_sha256_context *ctx, const uint8_t *data, size_t len) {
    sha256_update(ctx, data, len);
    return 0;
}

inline int mbedtls_sha256_finish_ret(mbedtls_sha256_context *ctx, uint8_t out[32]) {
    sha256_final(ctx, out);
    return 0;
}
