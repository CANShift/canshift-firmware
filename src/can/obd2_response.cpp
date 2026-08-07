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

uint8_t extractMode03Dtcs(const uint8_t *data, uint8_t length, uint8_t *out, uint8_t outCap) {
    constexpr uint8_t kHeaderBytes = 3; // len, 0x43, dtcCount
    if (data == nullptr || out == nullptr || length < kHeaderBytes)
        return 0;
    if (data[1] != kModeReadDtcResponse)
        return 0;

    const uint16_t declaredBytes = static_cast<uint16_t>(data[2]) * 2u;
    const uint16_t presentBytes = static_cast<uint16_t>(length - kHeaderBytes);
    uint16_t writeBytes = declaredBytes < presentBytes ? declaredBytes : presentBytes;
    if (writeBytes > outCap)
        writeBytes = outCap;
    writeBytes &= static_cast<uint16_t>(~1u); // whole DTC pairs only

    for (uint16_t i = 0; i < writeBytes; ++i)
        out[i] = data[kHeaderBytes + i];
    return static_cast<uint8_t>(writeBytes);
}

} // namespace Obd2Response
