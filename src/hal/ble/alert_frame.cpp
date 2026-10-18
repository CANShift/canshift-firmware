#include "hal/ble/alert_frame.h"

#include <cmath>
#include <cstring>

namespace AlertFrame {

namespace {

constexpr uint32_t kMsPerSecond = 1000;

int32_t scaleValue(float value) {
    return static_cast<int32_t>(std::lroundf(value * static_cast<float>(TelemetryFrame::kScale)));
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

size_t signalLength(const Entry &entry) {
    if (entry.signal == nullptr)
        return 0;
    return strnlen(entry.signal, kMaxSignalLen + 1);
}

bool isEncodable(const Entry &entry) {
    const size_t len = signalLength(entry);
    if (entry.source >= kSourceCount || len == 0 || len > kMaxSignalLen)
        return false;
    return std::isfinite(entry.value) && (!entry.hasLimit || std::isfinite(entry.limit));
}

uint8_t flagsOf(const Entry &entry) {
    uint8_t flags = 0;
    if (entry.hasLimit)
        flags |= kFlagHasLimit;
    if (entry.hasLimit && entry.below)
        flags |= kFlagBelow;
    if (entry.acknowledged)
        flags |= kFlagAcknowledged;
    if (entry.displayed)
        flags |= kFlagDisplayed;
    return flags;
}

void writeEntry(const Entry &entry, size_t len, uint8_t *p) {
    const uint32_t seconds =
        entry.activeSeconds < kMaxActiveSeconds ? entry.activeSeconds : kMaxActiveSeconds;
    p[0] = entry.source;
    p[1] = flagsOf(entry);
    writeInt32Le(p + 2, scaleValue(entry.value));
    writeInt32Le(p + 6, entry.hasLimit ? scaleValue(entry.limit) : 0);
    writeUint16Le(p + 10, static_cast<uint16_t>(seconds));
    p[12] = static_cast<uint8_t>(len);
    memcpy(p + kEntryFixedBytes, entry.signal, len);
}

}

size_t encode(const Entry *entries, size_t count, uint8_t *buf, size_t bufSize) {
    if (buf == nullptr || bufSize < kHeaderBytes)
        return 0;

    size_t offset = kHeaderBytes;
    uint8_t written = 0;
    for (size_t i = 0; entries != nullptr && i < count && written < kSourceCount; i++) {
        const size_t len = signalLength(entries[i]);
        if (!isEncodable(entries[i]) || offset + kEntryFixedBytes + len > bufSize)
            continue;
        writeEntry(entries[i], len, buf + offset);
        offset += kEntryFixedBytes + len;
        written++;
    }
    buf[0] = kVersion;
    buf[1] = written;
    return offset;
}

void trackOnsets(Onsets &onsets, uint32_t critical, uint32_t nowMs) {
    const uint32_t risen = critical & ~onsets.critical;
    for (uint8_t source = 0; source < kSourceCount; source++) {
        if ((risen & (1u << source)) != 0)
            onsets.sinceMs[source] = nowMs;
    }
    onsets.critical = critical;
}

uint32_t activeSeconds(const Onsets &onsets, uint8_t source, uint32_t nowMs) {
    if (source >= kSourceCount || (onsets.critical & (1u << source)) == 0)
        return 0;
    return (nowMs - onsets.sinceMs[source]) / kMsPerSecond;
}

void resetCadence(Cadence &cadence) {
    cadence = {};
}

bool shouldSend(Cadence &cadence, uint32_t generation) {
    if (cadence.ticksSinceSend < kKeepaliveTicks)
        cadence.ticksSinceSend++;
    const bool changed = !cadence.primed || generation != cadence.lastGeneration;
    if (!changed && cadence.ticksSinceSend < kKeepaliveTicks)
        return false;
    cadence = {true, generation, 0};
    return true;
}

}
