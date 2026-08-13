#pragma once

#include <stddef.h>
#include <stdint.h>

namespace canshift::touch::cst3530 {

inline constexpr size_t kFrameBytes = 32;
inline constexpr size_t kStatusByte = 2;
inline constexpr size_t kCountByte = 3;
inline constexpr size_t kFirstPointByte = 4;
inline constexpr size_t kPointStride = 5;
inline constexpr uint8_t kFrameReady = 0xFF;
inline constexpr uint16_t kChecksumSeed = 0x55;
inline constexpr uint8_t kMaxPoints = 5;

struct Sample {
    uint16_t x;
    uint16_t y;
    uint8_t pressure;
    uint8_t id;
};

inline bool checksumValid(const uint8_t *frame, size_t payloadLength) {
    if (kFirstPointByte + payloadLength > kFrameBytes) {
        return false;
    }
    uint16_t sum = kChecksumSeed;
    for (size_t i = 0; i < payloadLength; ++i) {
        sum = static_cast<uint16_t>(sum + frame[kFirstPointByte + i]);
    }
    return sum == static_cast<uint16_t>(frame[0] | (frame[1] << 8));
}

inline bool decodePoint(const uint8_t *point, Sample &out) {
    const uint8_t event = static_cast<uint8_t>(point[4] >> 4);
    if (event == 0) {
        return false;
    }
    const uint8_t high = point[3];
    out.x = static_cast<uint16_t>(point[0] | ((high & 0x0F) << 8));
    out.y = static_cast<uint16_t>(point[1] | ((high & 0xF0) << 4));
    out.pressure = point[2];
    out.id = static_cast<uint8_t>(point[4] & 0x0F);
    return true;
}

inline uint8_t decodeFrame(const uint8_t *frame, Sample *out, uint8_t capacity) {
    if (frame == nullptr || out == nullptr || capacity == 0) {
        return 0;
    }
    if (frame[kStatusByte] != kFrameReady) {
        return 0;
    }
    const uint8_t touched = static_cast<uint8_t>(frame[kCountByte] & 0x0F);
    const uint8_t keys = static_cast<uint8_t>((frame[kCountByte] & 0xF0) >> 4);
    if (touched == 0 || touched > kMaxPoints) {
        return 0;
    }
    if (!checksumValid(frame, static_cast<size_t>(keys + touched) * kPointStride)) {
        return 0;
    }

    uint8_t decoded = 0;
    for (uint8_t i = 0; i < touched && decoded < capacity; ++i) {
        const size_t base = kFirstPointByte + static_cast<size_t>(keys + i) * kPointStride;
        if (base + kPointStride > kFrameBytes) {
            break;
        }
        decoded = static_cast<uint8_t>(decoded + (decodePoint(&frame[base], out[decoded]) ? 1 : 0));
    }
    return decoded;
}

} // namespace canshift::touch::cst3530
