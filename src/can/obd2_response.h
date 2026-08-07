#pragma once

#include <stdint.h>

// Pure, dependency-free helpers for validating an OBD-II Mode 0x01 response
// frame (ISO 15765-2 single frame, `[len, mode|0x40, pid, data...]`). Kept out
// of obd2_poller so the framing rules can be exercised under native tests
// without the CAN / SignalStore stack.
namespace Obd2Response {

constexpr uint8_t kResponseModeMask = 0x40;
constexpr uint8_t kMinPayloadLen = 2;
constexpr uint8_t kMaxPayloadLen = 6;

struct Header {
    bool ok;
    uint8_t payloadLen;
    uint8_t mode;
    uint8_t pid;
};

[[nodiscard]] Header parseHeader(const uint8_t *data, uint8_t length);

[[nodiscard]] bool matches(const Header &header, uint8_t requestMode, uint8_t requestPid);

[[nodiscard]] bool fitsValueBytes(uint8_t startByte, uint8_t byteLength, uint8_t frameLength);

} // namespace Obd2Response
