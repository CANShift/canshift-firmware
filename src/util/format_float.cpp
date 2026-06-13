
#include "format_float.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if USE_RUST_FORMAT_FLOAT
    #include "format_float_rs.h"
#endif

namespace {

constexpr int kMaxDecimals = 9;
constexpr int kMaxSigDigits = 9;

constexpr int64_t POW10[] = {
    1LL, 10LL, 100LL, 1000LL, 10000LL, 100000LL, 1000000LL, 10000000LL, 100000000LL, 1000000000LL,
};
static_assert(sizeof(POW10) / sizeof(POW10[0]) == kMaxDecimals + 1, "POW10 table size");

size_t writeFixedWidthInt(char *buf, int64_t value, int digits) {
    for (int i = digits - 1; i >= 0; --i) {
        buf[i] = static_cast<char>('0' + (value % 10));
        value /= 10;
    }
    return static_cast<size_t>(digits);
}

size_t writeUnsignedInt(char *buf, int64_t value) {
    if (value == 0) {
        buf[0] = '0';
        return 1;
    }
    char tmp[24];
    int n = 0;
    while (value > 0 && n < static_cast<int>(sizeof(tmp))) {
        tmp[n++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    }
    for (int i = 0; i < n; ++i) {
        buf[i] = tmp[n - 1 - i];
    }
    return static_cast<size_t>(n);
}

size_t copyTerminated(char *buf, size_t size, const char *src) {
    if (size == 0)
        return 0;
    size_t srcLen = strlen(src);
    size_t copyLen = (srcLen < size - 1) ? srcLen : size - 1;
    memcpy(buf, src, copyLen);
    buf[copyLen] = '\0';
    return srcLen;
}

size_t formatFixedSigned(char *buf, size_t size, float value, int decimals) {
    if (decimals < 0)
        decimals = 0;
    if (decimals > kMaxDecimals)
        decimals = kMaxDecimals;

    if (isnan(value))
        return copyTerminated(buf, size, "nan");
    if (isinf(value))
        return copyTerminated(buf, size, value < 0 ? "-inf" : "inf");

    const bool negative = value < 0.0f;
    if (negative)
        value = -value;

    const int64_t scale = POW10[decimals];
    int64_t scaled = static_cast<int64_t>(value * static_cast<float>(scale) + 0.5f);

    const int64_t whole = scaled / scale;
    const int64_t frac = scaled % scale;

    char scratch[32];
    size_t pos = 0;
    if (negative)
        scratch[pos++] = '-';
    pos += writeUnsignedInt(scratch + pos, whole);
    if (decimals > 0) {
        scratch[pos++] = '.';
        pos += writeFixedWidthInt(scratch + pos, frac, decimals);
    }
    scratch[pos] = '\0';

    return copyTerminated(buf, size, scratch);
}

size_t formatWithSpec(char *buf, size_t size, float value, const char *spec) {
    if (spec == nullptr || spec[0] == '\0')
        return formatFixedSigned(buf, size, value, 1);

    const char *p = spec;
    const char *percent = nullptr;
    int decimals = -1;
    const char *afterSpec = nullptr;
    while (*p != '\0') {
        if (*p == '%' && *(p + 1) == '%') {

            p += 2;
            continue;
        }
        if (*p == '%') {
            const char *q = p + 1;
            int dec = 1;
            if (*q == '.') {
                ++q;
                int n = 0;
                int parsed = 0;
                while (*q >= '0' && *q <= '9') {
                    n = n * 10 + (*q - '0');
                    ++q;
                    ++parsed;
                    if (parsed > 2) {

                        n = -1;
                        break;
                    }
                }
                if (n < 0) {
                    p = q;
                    continue;
                }
                dec = n;
            }
            if (*q == 'f') {
                percent = p;
                decimals = dec;
                afterSpec = q + 1;
                break;
            }

            p = q + 1;
            continue;
        }
        ++p;
    }

    if (percent == nullptr) {

        return formatFixedSigned(buf, size, value, 1);
    }

    char number[32];
    formatFixedSigned(number, sizeof(number), value, decimals);

    if (size == 0)
        return 0;

    size_t prefixLen = static_cast<size_t>(percent - spec);
    size_t numberLen = strlen(number);
    size_t suffixLen = strlen(afterSpec);
    size_t total = prefixLen + numberLen + suffixLen;

    size_t out = 0;
    auto appendBounded = [&](const char *src, size_t len) {
        if (out >= size - 1)
            return;
        size_t room = size - 1 - out;
        size_t copy = (len < room) ? len : room;
        memcpy(buf + out, src, copy);
        out += copy;
    };
    appendBounded(spec, prefixLen);
    appendBounded(number, numberLen);
    appendBounded(afterSpec, suffixLen);
    buf[out] = '\0';
    return total;
}

} // namespace

namespace FloatFormat {

size_t formatFixed(char *buf, size_t size, float value, int decimals) {
    if (buf == nullptr || size == 0)
        return 0;
#if USE_RUST_FORMAT_FLOAT
    return format_fixed_rs(buf, size, value, decimals);
#else
    return formatFixedSigned(buf, size, value, decimals);
#endif
}

size_t formatFromSpec(char *buf, size_t size, float value, const char *spec) {
    if (buf == nullptr || size == 0)
        return 0;
#if USE_RUST_FORMAT_FLOAT
    return format_from_spec_rs(buf, size, value, spec);
#else
    return formatWithSpec(buf, size, value, spec);
#endif
}

size_t formatGeneral(char *buf, size_t size, float value, int sigDigits) {
    if (buf == nullptr || size == 0)
        return 0;
#if USE_RUST_FORMAT_FLOAT
    return format_general_rs(buf, size, value, sigDigits);
#else
    if (sigDigits < 1)
        sigDigits = 1;
    if (sigDigits > kMaxSigDigits)
        sigDigits = kMaxSigDigits;

    if (isnan(value))
        return copyTerminated(buf, size, "nan");
    if (isinf(value))
        return copyTerminated(buf, size, value < 0 ? "-inf" : "inf");

    float abs = value < 0.0f ? -value : value;

    int intDigits = 1;
    if (abs >= 1.0f) {
        float scan = abs;
        intDigits = 0;
        while (scan >= 1.0f && intDigits < 12) {
            scan /= 10.0f;
            ++intDigits;
        }
    }
    int decimals = sigDigits - intDigits;
    if (decimals < 0)
        decimals = 0;
    if (decimals > kMaxDecimals)
        decimals = kMaxDecimals;

    char scratch[32];
    size_t len = formatFixedSigned(scratch, sizeof(scratch), value, decimals);
    if (len >= sizeof(scratch))
        return copyTerminated(buf, size, scratch);

    if (decimals > 0) {
        size_t end = len;
        while (end > 0 && scratch[end - 1] == '0')
            --end;
        if (end > 0 && scratch[end - 1] == '.')
            --end;
        scratch[end] = '\0';
    }

    return copyTerminated(buf, size, scratch);
#endif
}

} // namespace FloatFormat
