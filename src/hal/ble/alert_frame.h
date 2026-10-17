#pragma once

#include "hal/ble/telemetry_frame.h"

#include <stddef.h>
#include <stdint.h>

namespace AlertFrame {

constexpr uint8_t kVersion = 0x02;
constexpr uint8_t kSourceCount = 11;
constexpr size_t kHeaderBytes = 2;
constexpr size_t kEntryFixedBytes = 13;
constexpr size_t kMaxSignalLen = 63;
constexpr size_t kMaxFrameBytes = 244;
constexpr uint32_t kMaxActiveSeconds = 0xFFFFu;
constexpr uint16_t kKeepaliveTicks = TelemetryFrame::kKeepaliveTicks;

constexpr uint8_t kFlagHasLimit = 0x01;
constexpr uint8_t kFlagBelow = 0x02;
constexpr uint8_t kFlagAcknowledged = 0x04;
constexpr uint8_t kFlagDisplayed = 0x08;

struct Entry {
    uint8_t source;
    float value;
    bool hasLimit;
    float limit;
    bool below;
    bool acknowledged;
    bool displayed;
    uint32_t activeSeconds;
    const char *signal;
};

size_t encode(const Entry *entries, size_t count, uint8_t *buf, size_t bufSize);

struct Onsets {
    uint32_t critical;
    uint32_t sinceMs[kSourceCount];
};

void trackOnsets(Onsets &onsets, uint32_t critical, uint32_t nowMs);

[[nodiscard]] uint32_t activeSeconds(const Onsets &onsets, uint8_t source, uint32_t nowMs);

struct Cadence {
    bool primed;
    uint32_t lastGeneration;
    uint16_t ticksSinceSend;
};

void resetCadence(Cadence &cadence);

bool shouldSend(Cadence &cadence, uint32_t generation);

} // namespace AlertFrame
