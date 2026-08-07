#pragma once

#include <stddef.h>
#include <stdint.h>

namespace TelemetryFrame {

constexpr uint8_t VERSION = 0x01;
constexpr int32_t SCALE = 1000;
constexpr size_t MAX_FIELDS = 14;
constexpr size_t HEADER_BYTES = 3;
constexpr size_t VALUE_BYTES = 4;
constexpr size_t MAX_FRAME_BYTES = HEADER_BYTES + MAX_FIELDS * VALUE_BYTES;

struct Field {
    bool present;
    float value;
};

size_t encode(const Field *fields, size_t count, uint8_t *buf, size_t bufSize);

} // namespace TelemetryFrame
