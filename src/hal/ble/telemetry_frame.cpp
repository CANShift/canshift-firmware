#include "hal/ble/telemetry_frame.h"

#include <cmath>
#include <cstddef>

namespace TelemetryFrame {

namespace {

int32_t scaleValue(float value) {
    return static_cast<int32_t>(std::lroundf(value * static_cast<float>(SCALE)));
}

void writeUint16Le(uint8_t *p, uint16_t value) {
    p[0] = static_cast<uint8_t>(value & 0xFFu);
    p[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
}

void writeInt32Le(uint8_t *p, int32_t value) {
    const uint32_t bits = static_cast<uint32_t>(value);
    p[0] = static_cast<uint8_t>(bits & 0xFFu);
    p[1] = static_cast<uint8_t>((bits >> 8) & 0xFFu);
    p[2] = static_cast<uint8_t>((bits >> 16) & 0xFFu);
    p[3] = static_cast<uint8_t>((bits >> 24) & 0xFFu);
}

bool isEmitted(const Field &field) {
    return field.present && std::isfinite(field.value);
}

} // namespace

size_t encode(const Field *fields, size_t count, uint8_t *buf, size_t bufSize) {
    if (fields == nullptr || buf == nullptr || count > MAX_FIELDS)
        return 0;

    uint16_t mask = 0;
    size_t present = 0;
    for (size_t i = 0; i < count; i++) {
        if (isEmitted(fields[i])) {
            mask |= static_cast<uint16_t>(1u << i);
            present++;
        }
    }

    const size_t total = HEADER_BYTES + present * VALUE_BYTES;
    if (bufSize < total)
        return 0;

    buf[0] = VERSION;
    writeUint16Le(buf + 1, mask);
    size_t offset = HEADER_BYTES;
    for (size_t i = 0; i < count; i++) {
        if (!isEmitted(fields[i]))
            continue;
        writeInt32Le(buf + offset, scaleValue(fields[i].value));
        offset += VALUE_BYTES;
    }
    return total;
}

} // namespace TelemetryFrame
