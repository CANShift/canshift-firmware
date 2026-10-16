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

constexpr uint32_t kChunkTimeoutMs = 10000;
constexpr uint32_t kBurnOverlayRenderGraceMs = 20;

struct ChunkTransfer {
    char path[kCfgMaxPathLen];
    uint32_t expectedIdx;
    uint32_t total;
    uint32_t lastActivityMs;
    bool fromSocket;
};
ChunkTransfer s_chunk = {{0}, 0, 0, 0, false};

void resetChunkState() {
    s_chunk.path[0] = '\0';
    s_chunk.expectedIdx = 0;
    s_chunk.total = 0;
    s_chunk.fromSocket = false;
}

} // namespace

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

} // namespace

void failChunk(const char *reason) {
    UsbComm::sendError(reason);
    abortChunkTransfer(reason);
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

    if (idx == 0) {
        abortChunkTransfer("new transfer");
        if (!StorageDriver::beginChunkedWriteAtomic(path)) {
            // No abort: the call above already cleared state and nothing is open.
            UsbComm::sendError("open_failed");
            return;
        }
        strlcpy(s_chunk.path, path, sizeof(s_chunk.path));
        s_chunk.total = total;
        s_chunk.expectedIdx = 0;
        s_chunk.fromSocket = UsbCommInternal::commandFromSocket();
    } else if (!StorageDriver::isChunkedWriteOpen() || s_chunk.expectedIdx != idx ||
               strcmp(s_chunk.path, path) != 0) {
        failChunk("out_of_sequence");
        return;
    }

    size_t decoded = 0;
    const int rc =
        mbedtls_base64_decode(reinterpret_cast<unsigned char *>(scratch.buf), scratch.cap, &decoded,
                              reinterpret_cast<const unsigned char *>(b64), strlen(b64));
    if (rc != 0) {
        failChunk("b64_decode");
        return;
    }

    if (!StorageDriver::appendChunk(reinterpret_cast<const uint8_t *>(scratch.buf), decoded)) {
        failChunk("write_failed");
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

namespace {

constexpr size_t kConfigChunkMaxRawBytes = 1024;

struct ConfigChunkState {
    uint32_t total;
    uint32_t expectedIdx;
    uint32_t lastActivityMs;
    uint32_t declaredBytes;
    uint32_t writtenBytes;
    char firstByte;
    char lastByte;
    bool open;
    bool fromSocket;
};

ConfigChunkState s_configChunk = {};

void abortConfigChunk(const char *reason) {
    if (!s_configChunk.open) {
        return;
    }
    LOG_WARN("USB", "PUT_CONFIG_CHUNK aborted: %s", reason);
    StorageDriver::abortChunkedWrite();
    s_configChunk = {};
    UsbCommInternal::invokeBurnOverlayShowError(0);
}

bool configChunkIdleExpired() {
    return s_configChunk.open && (millis() - s_configChunk.lastActivityMs) > kChunkTimeoutMs;
}

// Parsing a 16 KB config would overflow the USB task's 4 KB stack, so integrity
// is checked from what passed through: exact byte count, and the JSON delimiters.
bool receivedConfigLooksComplete() {
    if (s_configChunk.declaredBytes == 0 ||
        s_configChunk.writtenBytes != s_configChunk.declaredBytes) {
        LOG_ERROR("USB", "PUT_CONFIG_CHUNK: %u bytes received, %u announced",
                  static_cast<unsigned>(s_configChunk.writtenBytes),
                  static_cast<unsigned>(s_configChunk.declaredBytes));
        return false;
    }
    if (s_configChunk.firstByte != '{' || s_configChunk.lastByte != '}') {
        LOG_ERROR("USB", "PUT_CONFIG_CHUNK: payload is not a JSON object");
        return false;
    }
    return true;
}

} // namespace

void handlePutConfigChunk(const JsonObjectConst &obj) {
    const uint32_t total = obj["total"] | 0u;
    const uint32_t idx = obj["idx"] | 0u;
    const char *b64 = obj["data"];

    if (b64 == nullptr || total == 0 || idx >= total) {
        abortConfigChunk("bad_args");
        UsbComm::sendError("bad_args");
        return;
    }

    if (idx == 0) {
        abortConfigChunk("new transfer");
        if (!StorageDriver::beginChunkedWriteAtomic(kConfigPathDashboard)) {
            UsbComm::sendError("open_failed");
            return;
        }
        s_configChunk = {};
        s_configChunk.total = total;
        s_configChunk.lastActivityMs = millis();
        s_configChunk.declaredBytes = obj["bytes"] | 0u;
        s_configChunk.open = true;
        s_configChunk.fromSocket = UsbCommInternal::commandFromSocket();
        UsbCommInternal::invokeBurnOverlayShow();
    } else if (!s_configChunk.open || s_configChunk.expectedIdx != idx ||
               s_configChunk.total != total) {
        abortConfigChunk("out_of_sequence");
        UsbComm::sendError("out_of_sequence");
        return;
    }

    uint8_t raw[kConfigChunkMaxRawBytes];
    size_t decoded = 0;
    if (mbedtls_base64_decode(raw, sizeof(raw), &decoded,
                              reinterpret_cast<const unsigned char *>(b64), strlen(b64)) != 0) {
        abortConfigChunk("b64_decode");
        UsbComm::sendError("b64_decode");
        return;
    }

    if (!StorageDriver::appendChunk(raw, decoded)) {
        abortConfigChunk("write_failed");
        UsbComm::sendError("write_failed");
        return;
    }
    if (decoded > 0) {
        if (idx == 0) {
            s_configChunk.firstByte = static_cast<char>(raw[0]);
        }
        s_configChunk.lastByte = static_cast<char>(raw[decoded - 1]);
        s_configChunk.writtenBytes += decoded;
    }
    s_configChunk.expectedIdx = idx + 1;
    s_configChunk.lastActivityMs = millis();

    if (idx + 1 < total) {
        UsbComm::sendOk();
        return;
    }

    const bool complete = receivedConfigLooksComplete();
    const bool committed = complete && StorageDriver::endChunkedWrite();
    if (!complete) {
        StorageDriver::abortChunkedWrite();
    }
    s_configChunk = {};
    if (!committed) {
        UsbCommInternal::invokeBurnOverlayShowError(0);
        UsbComm::sendError("write_failed");
        return;
    }

    LOG_INFO("USB", "PUT_CONFIG_CHUNK: config replaced from %u chunks — reloading live",
             static_cast<unsigned>(total));
    PendingActions::configReload.store(true, std::memory_order_relaxed);
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
    PendingActions::configReload.store(true, std::memory_order_relaxed);
    UsbComm::sendOk();
}

void abortSocketTransfers(const char *reason) {
    if (s_chunk.total > 0 && s_chunk.fromSocket)
        abortChunkTransfer(reason);
    if (s_configChunk.fromSocket)
        abortConfigChunk(reason);
}

void tickChunkTransferTimeout() {
    if (s_chunk.total > 0 && StorageDriver::isChunkedWriteOpen() &&
        (millis() - s_chunk.lastActivityMs) > kChunkTimeoutMs) {
        abortChunkTransfer("idle timeout");
        return;
    }
    if (configChunkIdleExpired()) {
        abortConfigChunk("idle timeout");
    }
}

} // namespace UsbCommInternal
