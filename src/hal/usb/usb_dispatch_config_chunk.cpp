#include "usb_comm_internal.h"

#include "app_config.h"
#include "board_config.h"
#include "diag/logger.h"
#include "hal/storage/storage_driver.h"
#include "runtime/pending_actions.h"

#include <ArduinoJson.h>
#include <mbedtls/base64.h>

#ifdef ARDUINO
    #include <Arduino.h>
#endif

#include <stdint.h>
#include <string.h>

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

void failConfigChunk(const char *reason) {
    abortConfigChunk(reason);
    UsbComm::sendError(reason);
}

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

bool beginConfigChunk(const JsonObjectConst &obj, uint32_t total) {
    abortConfigChunk("new transfer");
    const uint32_t declaredBytes = obj["bytes"] | 0u;
    if (declaredBytes == 0) {
        UsbComm::sendError("bad_args");
        return false;
    }
    if (declaredBytes > kUsbPutConfigMaxBytes) {
        LOG_WARN("USB", "PUT_CONFIG_CHUNK: %u bytes announced, cap is %u",
                 static_cast<unsigned>(declaredBytes),
                 static_cast<unsigned>(kUsbPutConfigMaxBytes));
        UsbComm::sendError("too_large");
        return false;
    }
    if (!StorageDriver::beginChunkedWriteAtomic(kConfigPathDashboard)) {
        UsbComm::sendError("open_failed");
        return false;
    }
    s_configChunk = {};
    s_configChunk.total = total;
    s_configChunk.lastActivityMs = millis();
    s_configChunk.declaredBytes = declaredBytes;
    s_configChunk.open = true;
    s_configChunk.fromSocket = UsbCommInternal::commandFromSocket();
    UsbCommInternal::invokeBurnOverlayShow();
    return true;
}

bool continuesConfigChunk(uint32_t idx, uint32_t total) {
    return s_configChunk.open && s_configChunk.expectedIdx == idx && s_configChunk.total == total;
}

void recordConfigBytes(const uint8_t *raw, size_t decoded, uint32_t idx) {
    if (decoded == 0)
        return;
    if (idx == 0)
        s_configChunk.firstByte = static_cast<char>(raw[0]);
    s_configChunk.lastByte = static_cast<char>(raw[decoded - 1]);
    s_configChunk.writtenBytes += decoded;
}

const char *appendConfigChunk(const char *b64, uint32_t idx) {
    uint8_t raw[kConfigChunkMaxRawBytes];
    size_t decoded = 0;
    if (mbedtls_base64_decode(raw, sizeof(raw), &decoded,
                              reinterpret_cast<const unsigned char *>(b64), strlen(b64)) != 0)
        return "b64_decode";
    if (s_configChunk.writtenBytes + decoded > s_configChunk.declaredBytes)
        return "too_large";
    if (!StorageDriver::appendChunk(raw, decoded))
        return "write_failed";
    recordConfigBytes(raw, decoded, idx);
    s_configChunk.expectedIdx = idx + 1;
    s_configChunk.lastActivityMs = millis();
    return nullptr;
}

void commitConfigChunk(uint32_t total) {
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
    PendingActions::requestConfigReload();
    UsbComm::sendOk();
}

}

namespace UsbCommInternal {

void handlePutConfigChunk(const JsonObjectConst &obj) {
    const uint32_t total = obj["total"] | 0u;
    const uint32_t idx = obj["idx"] | 0u;
    const char *b64 = obj["data"];

    if (b64 == nullptr || total == 0 || idx >= total) {
        failConfigChunk("bad_args");
        return;
    }
    if (idx == 0 && !beginConfigChunk(obj, total))
        return;
    if (idx != 0 && !continuesConfigChunk(idx, total)) {
        failConfigChunk("out_of_sequence");
        return;
    }
    if (const char *failure = appendConfigChunk(b64, idx)) {
        failConfigChunk(failure);
        return;
    }
    if (idx + 1 < total) {
        UsbComm::sendOk();
        return;
    }
    commitConfigChunk(total);
}

void abortSocketConfigChunk(const char *reason) {
    if (s_configChunk.fromSocket)
        abortConfigChunk(reason);
}

bool socketConfigChunkOpen() {
    return s_configChunk.open && s_configChunk.fromSocket;
}

void tickConfigChunkTimeout() {
    if (s_configChunk.open && (millis() - s_configChunk.lastActivityMs) > kUsbChunkIdleTimeoutMs)
        abortConfigChunk("idle timeout");
}

}
