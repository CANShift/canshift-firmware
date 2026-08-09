#pragma once

#include <stdint.h>

namespace Obd2Dtc {

constexpr uint8_t kMaxDtcBytes = 32;

enum class Result : uint8_t { Ok, Timeout, SendFailed, Busy };

struct ReadOutcome {
    Result result;
    uint8_t byteCount;
};

[[nodiscard]] ReadOutcome read(uint8_t *out, uint8_t outCap, uint32_t timeoutMs);

[[nodiscard]] Result clear(uint32_t timeoutMs);

bool onRxFrame(uint32_t frameId, const uint8_t *data, uint8_t length);

namespace detail {

enum class Pending : uint8_t { None, Read, Clear };

[[nodiscard]] bool isPositiveResponse(Pending pending, const uint8_t *data, uint8_t length);

} // namespace detail

} // namespace Obd2Dtc
