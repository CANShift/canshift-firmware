#include "usb_comm_internal.h"
#include "usb_dispatch_validation.h"

#include "app_config.h"
#include "diag/logger.h"
#include "ota_signature_rs.h"
#include "diag/error_store.h"
#include "runtime/ota_receiver.h"
#include "runtime/ota_session_watch.h"
#include "runtime/pending_actions.h"
#include "runtime/workshop_mode.h"
#include "ui/ota_overlay.h"

#include <ArduinoJson.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <mbedtls/base64.h>

#ifdef ARDUINO
    #include <Arduino.h>
#endif

#include <atomic>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

bool s_sessionFromSocket = false;
uint32_t s_sessionActivityMs = 0;

void startSessionClock() {
    s_sessionFromSocket = UsbCommInternal::commandFromSocket();
    s_sessionActivityMs = millis();
}

struct VerdictText {
    OtaSessionWatch::Verdict verdict;
    const char *reason;
};

constexpr VerdictText kVerdictTexts[] = {
    {OtaSessionWatch::Verdict::IdleTimeout, "idle timeout"},
    {OtaSessionWatch::Verdict::LinkClosed, "tuner socket closed"},
};

const char *abortReasonFor(OtaSessionWatch::Verdict verdict) {
    for (const VerdictText &entry : kVerdictTexts) {
        if (entry.verdict == verdict)
            return entry.reason;
    }
    return nullptr;
}

void abortInterruptedSession(const char *reason) {
    LOG_ERROR("OTA", "Update interrupted: %s — keeping the running image", reason);
    ErrorStore::push(ERROR_SRC_SYSTEM, "OTA_STALL", reason);
    OtaReceiver::abort(reason);
    PendingActions::requestOtaOverlayFailed(
        static_cast<int8_t>(OtaOverlay::FailReason::Interrupted),
        static_cast<uint32_t>(OtaReceiver::writtenSize()));
}

bool parseOtaWriteFields(const char *jsonLine, uint32_t *offsetOut, const char **b64Out,
                         size_t *b64LenOut) {
    const char *offsetKey = strstr(jsonLine, "\"offset\":");
    if (offsetKey == nullptr)
        return false;
    char *endPtr = nullptr;
    const long parsed = strtol(offsetKey + 9, &endPtr, 10);
    if (endPtr == offsetKey + 9 || parsed < 0)
        return false;
    *offsetOut = static_cast<uint32_t>(parsed);

    const char *dataKey = strstr(jsonLine, "\"data\":\"");
    if (dataKey == nullptr)
        return false;
    const char *start = dataKey + 8;
    const char *end = strchr(start, '"');
    if (end == nullptr || end <= start)
        return false;
    *b64Out = start;
    *b64LenOut = static_cast<size_t>(end - start);
    return true;
}

enum class SignatureField : uint8_t { Missing, Malformed, Present };

SignatureField decodeSignature(const char *b64, uint8_t out[kOtaSignatureBytes]) {
    if (b64 == nullptr)
        return SignatureField::Missing;
    size_t decoded = 0;
    const int rc = mbedtls_base64_decode(out, kOtaSignatureBytes, &decoded,
                                         reinterpret_cast<const unsigned char *>(b64), strlen(b64));
    const bool whole = rc == 0 && decoded == kOtaSignatureBytes;
    return whole ? SignatureField::Present : SignatureField::Malformed;
}

void restartIntoCommittedImage() {
    if (UsbCommInternal::commandFromSocket())
        WorkshopMode::armResumeAfterUpdate();
    UsbComm::sendLine("{\"status\":\"ok\",\"restart\":true}");
    PendingActions::otaOverlayComplete.store(true, std::memory_order_relaxed);
    vTaskDelay(pdMS_TO_TICKS(kOtaCompleteScreenHoldMs));
    UsbComm::flushAndRestart();
}

} // namespace

namespace UsbCommInternal {

void handleOtaBegin(const JsonObjectConst &obj) {
    const uint32_t total = obj["total"] | 0u;
    const char *shaHex = obj["sha256"];
    uint8_t sha[UsbDispatchValidation::kSha256Bytes];
    if (total == 0 || !UsbDispatchValidation::parseSha256Hex(shaHex, sha)) {
        UsbComm::sendError("bad_args");
        return;
    }
    uint8_t signature[kOtaSignatureBytes];
    const SignatureField field = decodeSignature(obj["sig"], signature);
    if (field == SignatureField::Malformed) {
        UsbComm::sendError("ota_bad_signature");
        return;
    }
    const uint8_t *provided = field == SignatureField::Present ? signature : nullptr;
    const OtaReceiver::BeginResult result = OtaReceiver::begin(total, sha, provided);
    if (!result.ok) {
        UsbComm::sendError(result.error != nullptr ? result.error : "begin_failed");
        return;
    }
    startSessionClock();
    PendingActions::requestOtaOverlayShow(total, obj["version"] | "");
    UsbComm::sendOk();
}

void handleOtaWriteRaw(char *jsonLine) {
    uint32_t offset = 0;
    const char *b64 = nullptr;
    size_t b64Len = 0;
    if (!parseOtaWriteFields(jsonLine, &offset, &b64, &b64Len)) {
        UsbComm::sendError("bad_args");
        return;
    }
    configASSERT(b64 > jsonLine);
    const size_t decodeCap = static_cast<size_t>(b64 + b64Len - jsonLine);
    size_t decoded = 0;
    const int rc =
        mbedtls_base64_decode(reinterpret_cast<unsigned char *>(jsonLine), decodeCap, &decoded,
                              reinterpret_cast<const unsigned char *>(b64), b64Len);
    if (rc != 0 || decoded == 0) {
        UsbComm::sendError("b64_decode");
        OtaReceiver::abort("b64_decode");
        return;
    }
    const OtaReceiver::WriteResult result =
        OtaReceiver::writeChunk(offset, reinterpret_cast<const uint8_t *>(jsonLine), decoded);
    if (!result.ok) {
        char resp[96];
        snprintf(resp, sizeof(resp), "{\"status\":\"error\",\"message\":\"%s\",\"written\":%u}",
                 result.error != nullptr ? result.error : "write_failed",
                 static_cast<unsigned>(result.writtenTotal));
        UsbComm::sendLine(resp);
        PendingActions::requestOtaOverlayFailed(static_cast<int8_t>(OtaOverlay::FailReason::Write),
                                                static_cast<uint32_t>(result.writtenTotal));
        return;
    }
    s_sessionActivityMs = millis();
    OtaOverlay::setProgress(result.writtenTotal);
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"written\":%u}",
             static_cast<unsigned>(result.writtenTotal));
    UsbComm::sendLine(resp);
}

void handleOtaEnd(const JsonObjectConst &obj) {
    const char *action = obj["action"] | "abort";
    if (strcmp(action, "commit") == 0) {
        const OtaReceiver::CommitResult result = OtaReceiver::commit();
        if (!result.ok) {
            char resp[128];
            snprintf(resp, sizeof(resp),
                     "{\"status\":\"error\",\"message\":\"%s\",\"detail\":\"0x%x\"}",
                     result.error != nullptr ? result.error : "commit_failed",
                     static_cast<unsigned>(result.detailCode));
            UsbComm::sendLine(resp);
            PendingActions::requestOtaOverlayFailed(
                static_cast<int8_t>(OtaOverlay::FailReason::Commit),
                static_cast<uint32_t>(result.detailCode));
            return;
        }
        restartIntoCommittedImage();
        return;
    }
    OtaReceiver::abort("host_requested");
    PendingActions::requestOtaOverlayFailed(static_cast<int8_t>(OtaOverlay::FailReason::Aborted),
                                            0);
    UsbComm::sendOk();
}

} // namespace UsbCommInternal

void UsbCommInternal::tickOtaSessionWatch(bool socketClosed) {
    const OtaSessionWatch::Session session{OtaReceiver::state() == OtaReceiver::State::Receiving,
                                           s_sessionFromSocket, s_sessionActivityMs};
    const char *reason = abortReasonFor(OtaSessionWatch::judge(session, socketClosed, millis()));
    if (reason != nullptr)
        abortInterruptedSession(reason);
}
