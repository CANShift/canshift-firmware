#pragma once

#include <cstddef>
#include <cstdint>

inline constexpr int MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL = -0x002A;
inline constexpr int MBEDTLS_ERR_BASE64_INVALID_CHARACTER = -0x002C;

namespace bench_base64 {

inline int sextet(unsigned char c) {
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    return c == '/' ? 63 : -1;
}

inline size_t paddingOf(const unsigned char *src, size_t slen) {
    size_t pad = 0;
    while (pad < 2 && pad < slen && src[slen - 1 - pad] == '=')
        ++pad;
    return pad;
}

inline bool allValid(const unsigned char *src, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        if (sextet(src[i]) < 0)
            return false;
    }
    return true;
}

inline void decodeInto(unsigned char *dst, const unsigned char *src, size_t len) {
    uint32_t acc = 0;
    int bits = 0;
    size_t out = 0;
    for (size_t i = 0; i < len; ++i) {
        acc = (acc << 6) | static_cast<uint32_t>(sextet(src[i]));
        bits += 6;
        if (bits < 8)
            continue;
        bits -= 8;
        dst[out++] = static_cast<unsigned char>(acc >> bits);
    }
}

}

inline int mbedtls_base64_decode(unsigned char *dst, size_t dlen, size_t *olen,
                                 const unsigned char *src, size_t slen) {
    const size_t pad = bench_base64::paddingOf(src, slen);
    const size_t symbols = slen - pad;
    if ((slen % 4) != 0 || !bench_base64::allValid(src, symbols))
        return MBEDTLS_ERR_BASE64_INVALID_CHARACTER;
    const size_t needed = symbols * 6 / 8;
    *olen = needed;
    if (dst == nullptr || dlen < needed)
        return MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL;
    bench_base64::decodeInto(dst, src, symbols);
    return 0;
}
