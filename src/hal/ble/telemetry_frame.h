#pragma once

#include <stddef.h>
#include <stdint.h>

namespace TelemetryFrame {

constexpr uint8_t kVersion = 0x01;
constexpr int32_t kScale = 1000;
constexpr size_t kMaxFields = 14;
constexpr size_t kHeaderBytes = 3;
constexpr size_t kValueBytes = 4;
constexpr size_t kMaxFrameBytes = kHeaderBytes + kMaxFields * kValueBytes;

struct Field {
    bool present;
    float value;
};

size_t encode(const Field *fields, size_t count, uint8_t *buf, size_t bufSize);

constexpr uint16_t kKeepaliveTicks = 10;

struct Deduper {
    uint8_t last[kMaxFrameBytes];
    size_t lastLen;
    uint16_t ticksSinceSend;
};

void resetDeduper(Deduper &deduper);

bool shouldSend(Deduper &deduper, const uint8_t *frame, size_t len);

} // namespace TelemetryFrame
