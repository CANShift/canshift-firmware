#include "usb_dispatch_validation.h"

#include "board_config.h"
#include "config/config_types.h"

#include <string.h>

namespace UsbDispatchValidation {

namespace {

constexpr uint8_t kSha256HexLen = kSha256Bytes * 2;
constexpr unsigned char kAsciiControlMax = 0x1F;
constexpr unsigned char kAsciiDel = 0x7F;
constexpr int kHexNibbleShift = 4;
constexpr int kHexDecimalOffset = 10;

const char *const kAllowedPutPrefixes[] = {
    "/assets/",
    "/fonts/",
};

bool isAllowedPutTarget(const char *path, size_t len) {
    for (const char *prefix : kAllowedPutPrefixes) {
        const size_t plen = strlen(prefix);
        if (len > plen && strncmp(path, prefix, plen) == 0)
            return true;
    }
    return strcmp(path, kConfigPathSignals) == 0;
}

int hexNibble(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return kHexDecimalOffset + (c - 'a');
    if (c >= 'A' && c <= 'F')
        return kHexDecimalOffset + (c - 'A');
    return -1;
}

} // namespace

bool isPathSafe(const char *path) {
    if (!path)
        return false;
    const size_t len = strnlen(path, kCfgMaxPathLen);
    if (len == 0 || len >= kCfgMaxPathLen)
        return false;

    for (size_t i = 0; i < len; ++i) {
        const unsigned char c = static_cast<unsigned char>(path[i]);
        if (c <= kAsciiControlMax || c == kAsciiDel)
            return false;
    }
    if (strstr(path, "//"))
        return false;
    if (strstr(path, ".."))
        return false;
    return isAllowedPutTarget(path, len);
}

bool parseSha256Hex(const char *hex, uint8_t out[kSha256Bytes]) {
    if (hex == nullptr)
        return false;
    if (strlen(hex) != kSha256HexLen)
        return false;
    for (size_t i = 0; i < kSha256Bytes; ++i) {
        const int hi = hexNibble(hex[i * 2]);
        const int lo = hexNibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0)
            return false;
        out[i] = static_cast<uint8_t>((hi << kHexNibbleShift) | lo);
    }
    return true;
}

} // namespace UsbDispatchValidation
