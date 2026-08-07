#include "obd2_response.h"

namespace Obd2Response {

Header parseHeader(const uint8_t *data, uint8_t length) {
    if (data == nullptr || length < 3)
        return {false, 0, 0, 0};

    const uint8_t payloadLen = data[0];
    if (payloadLen < kMinPayloadLen || payloadLen > kMaxPayloadLen)
        return {false, 0, 0, 0};

    return {true, payloadLen, data[1], data[2]};
}

bool matches(const Header &header, uint8_t requestMode, uint8_t requestPid) {
    if (!header.ok)
        return false;
    const uint8_t expectedModeEcho = static_cast<uint8_t>(requestMode | kResponseModeMask);
    return header.mode == expectedModeEcho && header.pid == requestPid;
}

bool fitsValueBytes(uint8_t startByte, uint8_t byteLength, uint8_t frameLength) {
    return static_cast<uint16_t>(startByte) + static_cast<uint16_t>(byteLength) <= frameLength;
}

} // namespace Obd2Response
