#include "usb_dispatch_validation.h"

#include "config/config_types.h"

#include <string.h>

namespace UsbDispatchValidation {

namespace {

constexpr uint8_t SHA256_HEX_LEN = SHA256_BYTES * 2;
constexpr unsigned char ASCII_CONTROL_MAX = 0x1F;
constexpr unsigned char ASCII_DEL = 0x7F;
constexpr int HEX_NIBBLE_SHIFT = 4;
constexpr int HEX_DECIMAL_OFFSET = 10;

const char *const kAllowedPutPrefixes[] = {
    "/assets/",
    "/fonts/",
};

int hexNibble(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return HEX_DECIMAL_OFFSET + (c - 'a');
    if (c >= 'A' && c <= 'F')
        return HEX_DECIMAL_OFFSET + (c - 'A');
    return -1;
}

} // namespace

bool isPathSafe(const char *path) {
    if (!path)
        return false;
    const size_t len = strnlen(path, CFG_MAX_PATH_LEN);
    if (len == 0 || len >= CFG_MAX_PATH_LEN)
        return false;

    for (size_t i = 0; i < len; ++i) {
        const unsigned char c = static_cast<unsigned char>(path[i]);
        if (c <= ASCII_CONTROL_MAX || c == ASCII_DEL)
            return false;
    }
    if (strstr(path, "//"))
        return false;
    if (strstr(path, ".."))
        return false;
    for (const char *prefix : kAllowedPutPrefixes) {
        const size_t plen = strlen(prefix);
        if (len > plen && strncmp(path, prefix, plen) == 0)
            return true;
    }
    return false;
}

bool parseSha256Hex(const char *hex, uint8_t out[SHA256_BYTES]) {
    if (hex == nullptr)
        return false;
    if (strlen(hex) != SHA256_HEX_LEN)
        return false;
    for (size_t i = 0; i < SHA256_BYTES; ++i) {
        const int hi = hexNibble(hex[i * 2]);
        const int lo = hexNibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0)
            return false;
        out[i] = static_cast<uint8_t>((hi << HEX_NIBBLE_SHIFT) | lo);
    }
    return true;
}

} // namespace UsbDispatchValidation
