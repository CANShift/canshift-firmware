#pragma once

#include <stdint.h>

namespace Obd2Dtc {

constexpr uint8_t kMaxDtcBytes = 32;

enum class Result : uint8_t { Ok, Timeout, SendFailed, Busy };

struct Outcome {
    Result result;
    bool wasRead;
    uint8_t byteCount;
    uint8_t bytes[kMaxDtcBytes];
};

[[nodiscard]] bool beginRead(uint32_t timeoutMs);

[[nodiscard]] bool beginClear(uint32_t timeoutMs);

[[nodiscard]] bool isBusy();

// Non-blocking: true once the in-flight exchange resolved, either way.
[[nodiscard]] bool takeOutcome(Outcome *out);

bool onRxFrame(uint32_t frameId, const uint8_t *data, uint8_t length);

namespace detail {

enum class Pending : uint8_t { None, Read, Clear };

[[nodiscard]] bool isPositiveResponse(Pending pending, const uint8_t *data, uint8_t length);

} // namespace detail

} // namespace Obd2Dtc
