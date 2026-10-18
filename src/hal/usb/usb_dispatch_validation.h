#pragma once

#include <stdint.h>

namespace UsbDispatchValidation {

constexpr uint8_t kSha256Bytes = 32;

bool isPathSafe(const char *path);

bool parseSha256Hex(const char *hex, uint8_t out[kSha256Bytes]);

}
