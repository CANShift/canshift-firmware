#pragma once

#include <stdint.h>

namespace Obd2Response {

constexpr uint8_t kResponseModeMask = 0x40;
constexpr uint8_t kMinPayloadLen = 2;
constexpr uint8_t kMaxPayloadLen = 6;

constexpr uint8_t kModeReadDtcResponse = 0x43;

constexpr uint8_t kModeClearDtcResponse = 0x44;

constexpr uint8_t kRequestDlc = 8;

struct Header {
    bool ok;
    uint8_t payloadLen;
    uint8_t mode;
    uint8_t pid;
};

[[nodiscard]] Header parseHeader(const uint8_t *data, uint8_t length);

[[nodiscard]] bool matches(const Header &header, uint8_t requestMode, uint8_t requestPid);

[[nodiscard]] bool fitsValueBytes(uint8_t startByte, uint8_t byteLength, uint8_t frameLength);

[[nodiscard]] uint8_t extractMode03Dtcs(const uint8_t *data, uint8_t length, uint8_t *out,
                                        uint8_t outCap);

[[nodiscard]] bool buildServiceRequest(uint8_t mode, uint8_t *out, uint8_t outCap);

[[nodiscard]] bool isServiceResponse(const uint8_t *data, uint8_t length, uint8_t expectedService);

}
