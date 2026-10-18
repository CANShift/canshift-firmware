#include "usb_comm_internal.h"
#include "usb_dispatch_validation.h"
#include "usb_envelope.h"

#include "app_config.h"
#include "board_config.h"
#include "config/config_types.h"
#include "config/json_reader.h"
#include "diag/heap_stats.h"
#include "diag/logger.h"
#include "runtime/pending_actions.h"
#include "hal/storage/storage_driver.h"
#include "runtime/lvgl_lock.h"

#include <ArduinoJson.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <mbedtls/base64.h>

#ifdef ARDUINO
    #include <Arduino.h>
#endif

#include <stdint.h>
#include <string.h>

namespace {

constexpr uint32_t kBurnOverlayRenderGraceMs = 20;

struct ChunkTransfer {
    char path[kCfgMaxPathLen];
    uint32_t expectedIdx;
    uint32_t total;
    uint32_t lastActivityMs;
    uint32_t writtenBytes;
    bool fromSocket;
};
ChunkTransfer s_chunk = {{0}, 0, 0, 0, 0, false};

void resetChunkState() {
    s_chunk.path[0] = '\0';
    s_chunk.expectedIdx = 0;
    s_chunk.total = 0;
    s_chunk.writtenBytes = 0;
    s_chunk.fromSocket = false;
}

}

namespace UsbCommInternal {

void abortChunkTransfer(const char *reason) {
    if (StorageDriver::isChunkedWriteOpen()) {
        LOG_WARN("USB", "Aborting chunked transfer: %s (path=%s)", reason, s_chunk.path);
        StorageDriver::abortChunkedWrite();
    }
    resetChunkState();
}

namespace {

void finishPlainFile() {
    const bool finalized = StorageDriver::endChunkedWrite();
    if (!finalized) {
        UsbComm::sendError("write_failed");
        resetChunkState();
        return;
    }
    LOG_INFO("USB", "PUT_FILE done: %s (%u chunks)", s_chunk.path, s_chunk.total);
    resetChunkState();
    UsbComm::sendOk();
}

void finishChunkTransfer() {
    if (strcmp(s_chunk.path, kConfigPathSignals) == 0) {
        resetChunkState();
        UsbCommInternal::commitSignalsUpload();
        return;
    }
    finishPlainFile();
}

}

void failChunk(const char *reason) {
    UsbComm::sendError(reason);
    abortChunkTransfer(reason);
}

namespace {

bool beginPutFile(const char *path, uint32_t total) {
    abortChunkTransfer("new transfer");
    if (!StorageDriver::beginChunkedWriteAtomic(path)) {
        UsbComm::sendError("open_failed");
        return false;
    }
    strlcpy(s_chunk.path, path, sizeof(s_chunk.path));
    s_chunk.total = total;
    s_chunk.expectedIdx = 0;
    s_chunk.fromSocket = UsbCommInternal::commandFromSocket();
    return true;
}

bool continuesPutFile(const char *path, uint32_t idx) {
    return StorageDriver::isChunkedWriteOpen() && s_chunk.expectedIdx == idx &&
           strcmp(s_chunk.path, path) == 0;
}

const char *appendPutFileChunk(const char *b64, const DecodeScratch &scratch) {
    size_t decoded = 0;
    if (mbedtls_base64_decode(reinterpret_cast<unsigned char *>(scratch.buf), scratch.cap, &decoded,
                              reinterpret_cast<const unsigned char *>(b64), strlen(b64)) != 0)
        return "b64_decode";
    if (s_chunk.writtenBytes + decoded > kUsbPutFileMaxBytes)
        return "too_large";
    if (!StorageDriver::appendChunk(reinterpret_cast<const uint8_t *>(scratch.buf), decoded))
        return "write_failed";
    s_chunk.writtenBytes += decoded;
    return nullptr;
}

}

void handlePutFile(const JsonObjectConst &obj, const DecodeScratch &scratch) {
    const char *path = obj["path"];
    const uint32_t total = obj["total"] | 0u;
    const uint32_t idx = obj["idx"] | 0u;
    const char *b64 = obj["data"];

    if (!UsbDispatchValidation::isPathSafe(path) || !b64 || total == 0 || idx >= total) {
        failChunk("bad_args");
        return;
    }
    if (idx == 0 && !beginPutFile(path, total))
        return;
    if (idx != 0 && !continuesPutFile(path, idx)) {
        failChunk("out_of_sequence");
        return;
    }
    if (const char *failure = appendPutFileChunk(b64, scratch)) {
        failChunk(failure);
        return;
    }
    s_chunk.expectedIdx = idx + 1;
    s_chunk.lastActivityMs = millis();
    if (idx == total - 1) {
        finishChunkTransfer();
        return;
    }
    UsbComm::sendOk();
}

void handlePutConfig(const char *jsonLine) {
#ifdef ARDUINO
    HeapStats::logHeapBracket("PUT_CONFIG");
#endif

    const size_t lineLen = strlen(jsonLine);
    size_t written = 0;
    const char *payloadStart = UsbEnvelope::findPayloadSlice(jsonLine, lineLen, &written);
    if (!payloadStart || written == 0) {
        LOG_WARN("USB", "PUT_CONFIG: missing or malformed payload field");
        UsbComm::sendError("missing_payload");
        return;
    }

    UsbCommInternal::invokeBurnOverlayShow();
    vTaskDelay(pdMS_TO_TICKS(kBurnOverlayRenderGraceMs));

    LvglLock lock(pdMS_TO_TICKS(kUsbPutConfigMutexTimeoutMs));
    if (!lock.held()) {
        LOG_WARN("USB", "PUT_CONFIG: LVGL mutex busy — aborting write");
        UsbCommInternal::invokeBurnOverlayShowError(0);
        UsbComm::sendError("busy");
        return;
    }

    bool ok = StorageDriver::writeFileAtomic(
        kConfigPathDashboard, reinterpret_cast<const uint8_t *>(payloadStart), written);
    if (!ok) {
        UsbCommInternal::invokeBurnOverlayShowError(0);
        LOG_ERROR("USB", "PUT_CONFIG: storage write failed");
        UsbComm::sendError("write_failed");
        return;
    }

    LOG_INFO("USB", "PUT_CONFIG: dashboard.json updated (%u bytes) — reloading live", written);
    PendingActions::requestConfigReload();
    UsbComm::sendOk();
}

void abortSocketTransfers(const char *reason) {
    if (s_chunk.total > 0 && s_chunk.fromSocket)
        abortChunkTransfer(reason);
    abortSocketConfigChunk(reason);
}

bool socketFileTransferOpen() {
    return (s_chunk.total > 0 && s_chunk.fromSocket) || socketConfigChunkOpen();
}

void tickChunkTransferTimeout() {
    if (s_chunk.total > 0 && StorageDriver::isChunkedWriteOpen() &&
        (millis() - s_chunk.lastActivityMs) > kUsbChunkIdleTimeoutMs) {
        abortChunkTransfer("idle timeout");
        return;
    }
    tickConfigChunkTimeout();
}

}
