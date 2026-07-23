#include "usb_comm_internal.h"
#include "usb_dispatch_validation.h"
#include "usb_envelope.h"

#include "app_config.h"
#include "board_config.h"
#include "config/config_types.h"
#include "diag/heap_stats.h"
#include "diag/logger.h"
#include "hal/storage/storage_driver.h"

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

extern SemaphoreHandle_t g_lvglMutex;

namespace {

constexpr uint32_t CHUNK_TIMEOUT_MS = 10000;
constexpr uint32_t BURN_OVERLAY_RENDER_GRACE_MS = 20;

struct ChunkTransfer {
    char path[CFG_MAX_PATH_LEN];
    uint32_t expectedIdx;
    uint32_t total;
    uint32_t lastActivityMs;
};
ChunkTransfer s_chunk = {{0}, 0, 0, 0};

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
    s_chunk.path[0] = '\0';
    s_chunk.expectedIdx = 0;
    s_chunk.total = 0;
}

void handlePutFile(const JsonObjectConst &obj) {
    const char *path = obj["path"];
    const uint32_t total = obj["total"] | 0u;
    const uint32_t idx = obj["idx"] | 0u;
    const char *b64 = obj["data"];

    if (!UsbDispatchValidation::isPathSafe(path) || !b64 || total == 0 || idx >= total) {
        UsbComm::sendError("bad_args");
        abortChunkTransfer("bad_args");
        return;
    }

    if (idx == 0) {
        abortChunkTransfer("new transfer");
        if (!StorageDriver::beginChunkedWriteAtomic(path)) {
            UsbComm::sendError("open_failed");
            return;
        }
        strlcpy(s_chunk.path, path, sizeof(s_chunk.path));
        s_chunk.total = total;
        s_chunk.expectedIdx = 0;
    } else if (!StorageDriver::isChunkedWriteOpen() || s_chunk.expectedIdx != idx ||
               strcmp(s_chunk.path, path) != 0) {
        UsbComm::sendError("out_of_sequence");
        abortChunkTransfer("out_of_sequence");
        return;
    }

    size_t decoded = 0;
    const int rc = mbedtls_base64_decode(
        reinterpret_cast<unsigned char *>(UsbCommInternal::s_rxBuf), USB_RX_BUF_SIZE, &decoded,
        reinterpret_cast<const unsigned char *>(b64), strlen(b64));
    if (rc != 0) {
        UsbComm::sendError("b64_decode");
        abortChunkTransfer("b64_decode");
        return;
    }

    if (!StorageDriver::appendChunk(reinterpret_cast<const uint8_t *>(UsbCommInternal::s_rxBuf),
                                    decoded)) {
        UsbComm::sendError("write_failed");
        abortChunkTransfer("write_failed");
        return;
    }

    s_chunk.expectedIdx = idx + 1;
    s_chunk.lastActivityMs = millis();

    if (idx == total - 1) {
        const bool finalized = StorageDriver::endChunkedWrite();
        if (!finalized) {
            UsbComm::sendError("write_failed");
            s_chunk.path[0] = '\0';
            s_chunk.expectedIdx = 0;
            s_chunk.total = 0;
            return;
        }
        LOG_INFO("USB", "PUT_FILE done: %s (%u chunks)", s_chunk.path, total);
        s_chunk.path[0] = '\0';
        s_chunk.expectedIdx = 0;
        s_chunk.total = 0;
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
    vTaskDelay(pdMS_TO_TICKS(BURN_OVERLAY_RENDER_GRACE_MS));

    if (xSemaphoreTake(g_lvglMutex, pdMS_TO_TICKS(USB_PUT_CONFIG_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        LOG_WARN("USB", "PUT_CONFIG: LVGL mutex busy — aborting write");
        UsbCommInternal::invokeBurnOverlayShowError(0);
        UsbComm::sendError("busy");
        return;
    }

    bool ok = StorageDriver::writeFileAtomic(
        CONFIG_PATH_DASHBOARD, reinterpret_cast<const uint8_t *>(payloadStart), written);
    if (!ok) {
        xSemaphoreGive(g_lvglMutex);

        UsbCommInternal::invokeBurnOverlayShowError(0);
        LOG_ERROR("USB", "PUT_CONFIG: storage write failed");
        UsbComm::sendError("write_failed");
        return;
    }

    LOG_INFO("USB", "PUT_CONFIG: dashboard.json updated (%u bytes) — rebooting", written);
    UsbComm::sendOkRebooting();
    Serial.flush();
    esp_restart();
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
    static constexpr size_t kEnvelopeOverhead = 32;
    const size_t needed = fileBytes + kEnvelopeOverhead;
    if (needed > USB_RX_BUF_SIZE) {
        LOG_ERROR("USB", "GET_CONFIG: response %u B exceeds rxBuf %u B",
                  static_cast<unsigned>(needed), static_cast<unsigned>(USB_RX_BUF_SIZE));
        UsbComm::sendError("too_large");
        return;
    }
    if (UsbCommInternal::s_rxBuf == nullptr) {
        UsbComm::sendError("rxbuf_unavailable");
        return;
    }
    char *buf = UsbCommInternal::s_rxBuf;
    static const char kPrefix[] = "{\"status\":\"ok\",\"config\":";
    constexpr size_t kPrefixLen = sizeof(kPrefix) - 1;
    memcpy(buf, kPrefix, kPrefixLen);

    BufferPrint sink(buf + kPrefixLen, needed - kPrefixLen - 2);
    const size_t streamed = StorageDriver::streamFileTo(CONFIG_PATH_DASHBOARD, sink, true);

    if (streamed == 0 || sink.used() == 0 || streamed != sink.used()) {
        LOG_WARN("USB", "GET_CONFIG: stream mismatch for %s (streamed=%u, sink=%u)",
                 CONFIG_PATH_DASHBOARD, static_cast<unsigned>(streamed),
                 static_cast<unsigned>(sink.used()));
        UsbComm::sendError("config_read_failed");
        return;
    }
    size_t pos = kPrefixLen + sink.used();
    buf[pos++] = '}';
    buf[pos] = '\0';
    UsbComm::sendLine(buf);

    LOG_INFO("USB", "GET_CONFIG: sent %u bytes", static_cast<unsigned>(streamed));
}

void tickChunkTransferTimeout() {
    if (StorageDriver::isChunkedWriteOpen() &&
        (millis() - s_chunk.lastActivityMs) > CHUNK_TIMEOUT_MS) {
        abortChunkTransfer("idle timeout");
    }
}

} // namespace UsbCommInternal
