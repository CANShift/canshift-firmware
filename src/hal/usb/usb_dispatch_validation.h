#pragma once

#include <stdint.h>

namespace UsbDispatchValidation {

constexpr uint8_t SHA256_BYTES = 32;

bool isPathSafe(const char *path);

bool parseSha256Hex(const char *hex, uint8_t out[SHA256_BYTES]);

} // namespace UsbDispatchValidation
