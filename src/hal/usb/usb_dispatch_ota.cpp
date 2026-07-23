#include "usb_comm_internal.h"
#include "usb_dispatch_validation.h"

#include "app_config.h"
#include "diag/logger.h"
#include "runtime/ota_receiver.h"
#include "runtime/pending_actions.h"
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

} // namespace

namespace UsbCommInternal {

void handleOtaBegin(const JsonObjectConst &obj) {
    const uint32_t total = obj["total"] | 0u;
    const char *shaHex = obj["sha256"];
    uint8_t sha[UsbDispatchValidation::SHA256_BYTES];
    if (total == 0 || !UsbDispatchValidation::parseSha256Hex(shaHex, sha)) {
        UsbComm::sendLine("{\"status\":\"error\",\"message\":\"bad_args\"}");
        return;
    }
    const OtaReceiver::BeginResult result = OtaReceiver::begin(total, sha);
    if (!result.ok) {
        char resp[80];
        snprintf(resp, sizeof(resp), "{\"status\":\"error\",\"message\":\"%s\"}",
                 result.error != nullptr ? result.error : "begin_failed");
        UsbComm::sendLine(resp);
        return;
    }
    PendingActions::otaOverlayShowSize.store(total, std::memory_order_relaxed);
    UsbComm::sendLine("{\"status\":\"ok\"}");
}

void handleOtaWriteRaw(const char *jsonLine) {
    uint32_t offset = 0;
    const char *b64 = nullptr;
    size_t b64Len = 0;
    if (!parseOtaWriteFields(jsonLine, &offset, &b64, &b64Len)) {
        UsbComm::sendLine("{\"status\":\"error\",\"message\":\"bad_args\"}");
        return;
    }
    // In-place decode aliases s_rxBuf: safe only while the output base stays below
    // the b64 input, so the write index (3 B out per 4 B in) never overtakes the read.
    configASSERT(b64 > UsbCommInternal::s_rxBuf &&
                 b64 + b64Len <= UsbCommInternal::s_rxBuf + USB_RX_BUF_SIZE);
    size_t decoded = 0;
    const int rc = mbedtls_base64_decode(
        reinterpret_cast<unsigned char *>(UsbCommInternal::s_rxBuf), USB_RX_BUF_SIZE, &decoded,
        reinterpret_cast<const unsigned char *>(b64), b64Len);
    if (rc != 0 || decoded == 0) {
        UsbComm::sendLine("{\"status\":\"error\",\"message\":\"b64_decode\"}");
        OtaReceiver::abort("b64_decode");
        return;
    }
    const OtaReceiver::WriteResult result = OtaReceiver::writeChunk(
        offset, reinterpret_cast<const uint8_t *>(UsbCommInternal::s_rxBuf), decoded);
    if (!result.ok) {
        char resp[96];
        snprintf(resp, sizeof(resp), "{\"status\":\"error\",\"message\":\"%s\",\"written\":%u}",
                 result.error != nullptr ? result.error : "write_failed",
                 static_cast<unsigned>(result.writtenTotal));
        UsbComm::sendLine(resp);
        return;
    }
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
            PendingActions::otaOverlayHide.store(true, std::memory_order_relaxed);
            return;
        }
        UsbComm::sendLine("{\"status\":\"ok\",\"restart\":true}");
        delay(50);
        esp_restart();
        return;
    }
    OtaReceiver::abort("host_requested");
    PendingActions::otaOverlayHide.store(true, std::memory_order_relaxed);
    UsbComm::sendLine("{\"status\":\"ok\"}");
}

} // namespace UsbCommInternal
