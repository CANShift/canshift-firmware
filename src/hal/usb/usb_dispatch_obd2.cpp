#include "usb_comm_internal.h"

#include "can/obd2_dtc.h"
#include "diag/logger.h"

#include <stdint.h>
#include <stdio.h>

namespace {

constexpr uint32_t kReadTimeoutMs = 1500;
constexpr uint32_t kClearTimeoutMs = 1500;
constexpr size_t kResponseCap = 160;

const char *failureReason(Obd2Dtc::Result result) {
    switch (result) {
        case Obd2Dtc::Result::Timeout:
            return "obd_no_response";
        case Obd2Dtc::Result::SendFailed:
            return "obd_send_failed";
        case Obd2Dtc::Result::Busy:
            return "obd_busy";
        case Obd2Dtc::Result::Ok:
            return "ok";
    }
    return "obd_failed";
}

} // namespace

namespace UsbCommInternal {

void handleObdReadDtc() {
    uint8_t bytes[Obd2Dtc::kMaxDtcBytes];
    const Obd2Dtc::ReadOutcome outcome = Obd2Dtc::read(bytes, sizeof bytes, kReadTimeoutMs);

    if (outcome.result != Obd2Dtc::Result::Ok) {
        UsbComm::sendError(failureReason(outcome.result));
        return;
    }

    char resp[kResponseCap];
    int n = snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"dtc_bytes\":[");
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(resp)) {
        UsbComm::sendError("obd_response_truncated");
        return;
    }

    for (uint8_t i = 0; i < outcome.byteCount; ++i) {
        const int written = snprintf(resp + n, sizeof(resp) - static_cast<size_t>(n), "%s%u",
                                     i == 0 ? "" : ",", static_cast<unsigned>(bytes[i]));
        if (written <= 0 || static_cast<size_t>(n) + static_cast<size_t>(written) >= sizeof(resp)) {
            UsbComm::sendError("obd_response_truncated");
            return;
        }
        n += written;
    }

    const int tail = snprintf(resp + n, sizeof(resp) - static_cast<size_t>(n), "]}");
    if (tail <= 0 || static_cast<size_t>(n) + static_cast<size_t>(tail) >= sizeof(resp)) {
        UsbComm::sendError("obd_response_truncated");
        return;
    }

    UsbComm::sendLine(resp);
}

void handleObdClearDtc() {
    const Obd2Dtc::Result result = Obd2Dtc::clear(kClearTimeoutMs);
    if (result != Obd2Dtc::Result::Ok) {
        UsbComm::sendError(failureReason(result));
        return;
    }
    UsbComm::sendLine("{\"status\":\"ok\"}");
}

} // namespace UsbCommInternal
