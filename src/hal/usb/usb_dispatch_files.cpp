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
    char path[CFG_MAX_PATH_LEN];
    uint32_t expectedIdx;
    uint32_t total;
    uint32_t lastActivityMs;
};
ChunkTransfer s_chunk = {{0}, 0, 0, 0};

void resetChunkState() {
    s_chunk.path[0] = '\0';
    s_chunk.expectedIdx = 0;
    s_chunk.total = 0;
}

class SerialPrint : public Print {
  public:
    size_t write(uint8_t b) override {
        return Logger::writeAll(&b, 1) ? 1 : 0;
    }
    size_t write(const uint8_t *data, size_t len) override {
        return Logger::writeAll(data, len) ? len : 0;
    }
};

class BufferPrint : public Print {
  public:
    BufferPrint(char *dst, size_t cap) : dst_(dst), cap_(cap), used_(0) {}
    size_t write(uint8_t b) override {
        if (used_ >= cap_)
            return 0;
        dst_[used_++] = static_cast<char>(b);
        return 1;
    }
    size_t write(const uint8_t *data, size_t len) override {
        const size_t room = cap_ - used_;
        const size_t n = len < room ? len : room;
        memcpy(dst_ + used_, data, n);
        used_ += n;
        return n;
    }
    size_t used() const {
        return used_;
    }

  private:
    char *dst_;
    size_t cap_;
    size_t used_;
};

} // namespace

namespace UsbCommInternal {

void abortChunkTransfer(const char *reason) {
    if (StorageDriver::isChunkedWriteOpen()) {
        LOG_WARN("USB", "Aborting chunked transfer: %s (path=%s)", reason, s_chunk.path);
        StorageDriver::abortChunkedWrite();
    }
    resetChunkState();
}

void failChunk(const char *reason) {
    UsbComm::sendError(reason);
    abortChunkTransfer(reason);
}

void handlePutFile(const JsonObjectConst &obj) {
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
    } else if (!StorageDriver::isChunkedWriteOpen() || s_chunk.expectedIdx != idx ||
               strcmp(s_chunk.path, path) != 0) {
        failChunk("out_of_sequence");
        return;
    }

    size_t decoded = 0;
    const int rc = mbedtls_base64_decode(
        reinterpret_cast<unsigned char *>(UsbCommInternal::s_rxBuf), USB_RX_BUF_SIZE, &decoded,
        reinterpret_cast<const unsigned char *>(b64), strlen(b64));
    if (rc != 0) {
        failChunk("b64_decode");
        return;
    }

    if (!StorageDriver::appendChunk(reinterpret_cast<const uint8_t *>(UsbCommInternal::s_rxBuf),
                                    decoded)) {
        failChunk("write_failed");
        return;
    }

    s_chunk.expectedIdx = idx + 1;
    s_chunk.lastActivityMs = millis();

    if (idx == total - 1) {
        const bool finalized = StorageDriver::endChunkedWrite();
        if (!finalized) {
            // endChunkedWrite already closed the handle, so this is a reset, not an abort.
            UsbComm::sendError("write_failed");
            resetChunkState();
            return;
        }
        LOG_INFO("USB", "PUT_FILE done: %s (%u chunks)", s_chunk.path, total);
        resetChunkState();
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
        if (!StorageDriver::beginChunkedWriteAtomic(CONFIG_PATH_DASHBOARD)) {
            UsbComm::sendError("open_failed");
            return;
        }
        s_configChunk = {};
        s_configChunk.total = total;
        s_configChunk.lastActivityMs = millis();
        s_configChunk.declaredBytes = obj["bytes"] | 0u;
        s_configChunk.open = true;
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

    LvglLock lock(pdMS_TO_TICKS(USB_PUT_CONFIG_MUTEX_TIMEOUT_MS));
    if (!lock.held()) {
        LOG_WARN("USB", "PUT_CONFIG: LVGL mutex busy — aborting write");
        UsbCommInternal::invokeBurnOverlayShowError(0);
        UsbComm::sendError("busy");
        return;
    }

    bool ok = StorageDriver::writeFileAtomic(
        CONFIG_PATH_DASHBOARD, reinterpret_cast<const uint8_t *>(payloadStart), written);
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

void handleGetConfig() {
    if (!StorageDriver::fileExists(CONFIG_PATH_DASHBOARD)) {
        UsbComm::sendError("config_not_found");
        return;
    }
    const size_t fileBytes = StorageDriver::fileSize(CONFIG_PATH_DASHBOARD);
    if (fileBytes == 0) {
        LOG_WARN("USB", "GET_CONFIG: empty / unreadable %s", CONFIG_PATH_DASHBOARD);
        UsbComm::sendError("config_not_found");
        return;
    }
    static const char kPrefix[] = "{\"status\":\"ok\",\"config\":";
    static const char kSuffix[] = "}\n";

    if (!Logger::lockUart(pdMS_TO_TICKS(USB_TX_LOCK_TIMEOUT_MS))) {
        LOG_WARN("USB", "GET_CONFIG: serial busy");
        UsbComm::sendError("busy");
        return;
    }

    SerialPrint sink;
    const bool prefixOk =
        Logger::writeAll(reinterpret_cast<const uint8_t *>(kPrefix), sizeof(kPrefix) - 1);
    const size_t streamed =
        prefixOk ? StorageDriver::streamFileTo(CONFIG_PATH_DASHBOARD, sink, true) : 0;
    const bool suffixOk =
        streamed > 0 &&
        Logger::writeAll(reinterpret_cast<const uint8_t *>(kSuffix), sizeof(kSuffix) - 1);
    Logger::unlockUart();

    if (!suffixOk) {
        LOG_ERROR("USB", "GET_CONFIG: stream to host failed after %u B",
                  static_cast<unsigned>(streamed));
        return;
    }

    LOG_INFO("USB", "GET_CONFIG: sent %u bytes", static_cast<unsigned>(streamed));
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
