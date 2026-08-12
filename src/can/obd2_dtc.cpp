#include "can/obd2_dtc.h"

#include "app_config.h"
#include "can/can_manager.h"
#include "can/obd2_response.h"
#include "diag/logger.h"

#include <Arduino.h>
#include <atomic>
#include <string.h>

namespace {

constexpr uint8_t kModeReadDtc = 0x03;
constexpr uint8_t kModeClearDtc = 0x04;

std::atomic<uint8_t> s_pending{static_cast<uint8_t>(Obd2Dtc::detail::Pending::None)};
std::atomic<bool> s_responded{false};

uint8_t s_dtcBytes[Obd2Dtc::kMaxDtcBytes];
uint8_t s_dtcByteCount = 0;
uint32_t s_deadlineMs = 0;

Obd2Dtc::detail::Pending pendingState() {
    return static_cast<Obd2Dtc::detail::Pending>(s_pending.load(std::memory_order_acquire));
}

void setPending(Obd2Dtc::detail::Pending next) {
    s_pending.store(static_cast<uint8_t>(next), std::memory_order_release);
}

bool beginExchange(Obd2Dtc::detail::Pending kind, uint8_t mode, uint32_t timeoutMs) {
    if (pendingState() != Obd2Dtc::detail::Pending::None)
        return false;

    s_dtcByteCount = 0;
    s_responded.store(false, std::memory_order_release);
    s_deadlineMs = millis() + timeoutMs;
    setPending(kind);

    uint8_t payload[Obd2Response::kRequestDlc];
    if (!Obd2Response::buildServiceRequest(mode, payload, sizeof payload)) {
        setPending(Obd2Dtc::detail::Pending::None);
        return false;
    }
    if (!CanManager::sendFrame(OBD2_REQUEST_FRAME_ID, payload, Obd2Response::kRequestDlc, false)) {
        setPending(Obd2Dtc::detail::Pending::None);
        return false;
    }
    return true;
}

} // namespace

namespace Obd2Dtc {

namespace detail {

bool isPositiveResponse(Pending pending, const uint8_t *data, uint8_t length) {
    if (pending == Pending::Read)
        return Obd2Response::isServiceResponse(data, length, Obd2Response::kModeReadDtcResponse);
    if (pending == Pending::Clear)
        return Obd2Response::isServiceResponse(data, length, Obd2Response::kModeClearDtcResponse);
    return false;
}

} // namespace detail

bool beginRead(uint32_t timeoutMs) {
    return beginExchange(detail::Pending::Read, kModeReadDtc, timeoutMs);
}

bool beginClear(uint32_t timeoutMs) {
    return beginExchange(detail::Pending::Clear, kModeClearDtc, timeoutMs);
}

bool isBusy() {
    return pendingState() != detail::Pending::None;
}

bool takeOutcome(Outcome *out) {
    if (out == nullptr)
        return false;
    const detail::Pending pending = pendingState();
    if (pending == detail::Pending::None)
        return false;

    const bool wasRead = (pending == detail::Pending::Read);
    if (s_responded.load(std::memory_order_acquire)) {
        out->result = Result::Ok;
        out->wasRead = wasRead;
        out->byteCount = s_dtcByteCount;
        memcpy(out->bytes, s_dtcBytes, s_dtcByteCount);
        setPending(detail::Pending::None);
        return true;
    }
    if (static_cast<int32_t>(millis() - s_deadlineMs) < 0)
        return false;

    out->result = Result::Timeout;
    out->wasRead = wasRead;
    out->byteCount = 0;
    setPending(detail::Pending::None);
    return true;
}

bool onRxFrame(uint32_t frameId, const uint8_t *data, uint8_t length) {
    const detail::Pending pending = pendingState();
    if (pending == detail::Pending::None)
        return false;
    if (frameId != OBD2_RESPONSE_FRAME_ID)
        return false;
    if (!detail::isPositiveResponse(pending, data, length))
        return false;

    if (pending == detail::Pending::Read) {
        s_dtcByteCount = Obd2Response::extractMode03Dtcs(data, length, s_dtcBytes, kMaxDtcBytes);
        LOG_INFO("OBD2", "DTC read: %u code byte(s)", static_cast<unsigned>(s_dtcByteCount));
    } else {
        LOG_INFO("OBD2", "DTC clear acknowledged");
    }

    s_responded.store(true, std::memory_order_release);
    return true;
}

} // namespace Obd2Dtc
