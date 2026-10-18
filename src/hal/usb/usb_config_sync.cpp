
#include "usb_comm_internal.h"

#include "app_config.h"
#include "board_config.h"
#include "diag/logger.h"
#include "hal/storage/storage_driver.h"
#include "runtime/workshop_mode.h"
#include "util/mem_alloc.h"
#include "util/json_writer.h"

#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

#ifdef ARDUINO
    #include <Arduino.h>
#endif

#include <stdlib.h>
#include <string.h>

namespace {

class ChunkedAtomicWriter : public Print {
  public:
    size_t write(uint8_t b) override {
        return StorageDriver::appendChunk(&b, 1) ? 1 : 0;
    }
    size_t write(const uint8_t *buffer, size_t size) override {
        return StorageDriver::appendChunk(buffer, size) ? size : 0;
    }
};

bool writeAtomically(const char *path, const JsonDocument &out, size_t projected) {
    if (!StorageDriver::beginChunkedWriteAtomic(path)) {
        LOG_ERROR("USB", "PUT %s: open failed", path);
        return false;
    }
    ChunkedAtomicWriter writer;
    const size_t written = JsonWriter::toPrint(out, writer);
    if (written != projected) {
        StorageDriver::abortChunkedWrite();
        LOG_ERROR("USB", "PUT %s: stream failed (%u/%u bytes)", path,
                  static_cast<unsigned>(written), static_cast<unsigned>(projected));
        return false;
    }
    if (!StorageDriver::endChunkedWrite()) {
        LOG_ERROR("USB", "PUT %s: commit failed", path);
        return false;
    }
    return true;
}

void persistTypedConfigAndReboot(const char *path, const char *wrapKey, JsonVariantConst subValue) {
    JsonDocument out;
    if (wrapKey)
        out[wrapKey] = subValue;
    else
        out.set(subValue);
    const size_t projected = JsonWriter::measure(out);
    if (projected == 0 || projected > UsbCommInternal::kTypedPutMaxPayloadBytes) {
        LOG_WARN("USB", "PUT %s: serialized size %u out of bounds", path,
                 static_cast<unsigned>(projected));
        UsbComm::sendError("too_large");
        return;
    }
    if (!writeAtomically(path, out, projected)) {
        UsbComm::sendError("write_failed");
        return;
    }
    LOG_INFO("USB", "PUT %s: %u bytes written — rebooting", path, static_cast<unsigned>(projected));
    UsbCommInternal::rebootIntoSavedConfig();
}

const char *readTypedBody(const char *path, const char *fieldKey, TypedConfigBody::BodyOf bodyOf,
                          JsonDocument &fileDoc, JsonVariantConst &body) {
    if (!StorageDriver::fileExists(path))
        return "config_not_found";
    const DeserializationError err = StorageDriver::parseJsonFile(path, fileDoc);
    if (err) {
        LOG_WARN("USB", "GET %s parse error: %s", path, err.c_str());
        return "parse_error";
    }
    body = bodyOf(fileDoc);
    if (body.isNull()) {
        LOG_WARN("USB", "GET %s: no '%s' body in the file", path, fieldKey);
        return "config_not_found";
    }
    return nullptr;
}

void sendDocument(const char *path, const JsonDocument &resp) {
    const size_t needed = JsonWriter::measure(resp) + 1;
    char *buf = static_cast<char *>(Mem::allocPreferSpiram(needed));
    if (!buf) {
        LOG_ERROR("USB", "GET %s: response buffer alloc (%u B) failed", path,
                  static_cast<unsigned>(needed));
        UsbComm::sendError("oom");
        return;
    }
    const size_t written = JsonWriter::toBuffer(resp, buf, needed);
    buf[written] = '\0';
    UsbComm::sendLine(buf);
    heap_caps_free(buf);
}

}

void UsbCommInternal::rebootIntoSavedConfig() {
    if (commandFromSocket())
        WorkshopMode::armResumeAfterConfigChange();
    UsbComm::sendOkRebooting();
    UsbComm::flushAndRestart();
}

namespace UsbCommInternal {

void sendTypedConfigGet(const char *path, const char *fieldKey, TypedConfigBody::BodyOf bodyOf) {
    JsonDocument fileDoc;
    JsonVariantConst body;
    const char *failure = readTypedBody(path, fieldKey, bodyOf, fileDoc, body);
    if (failure != nullptr) {
        UsbComm::sendError(failure);
        return;
    }
    JsonDocument resp;
    resp["status"] = "ok";
    resp[fieldKey] = body;
    sendDocument(path, resp);
}

void handlePutDeviceConfig(const JsonObjectConst &obj) {
    JsonVariantConst sub = obj[TypedConfigBody::kDeviceConfigKey];
    if (sub.isNull() || !sub.is<JsonObjectConst>()) {
        UsbComm::sendError("missing_device_config");
        return;
    }
    persistTypedConfigAndReboot(kConfigPathDevice, nullptr, sub);
}

void handlePutInputBindings(const JsonObjectConst &obj) {
    JsonVariantConst sub = obj[TypedConfigBody::kInputBindingsKey];
    if (sub.isNull() || !sub.is<JsonArrayConst>()) {
        UsbComm::sendError("missing_input_bindings");
        return;
    }
    persistTypedConfigAndReboot(kConfigPathInputs, TypedConfigBody::kInputBindingsKey, sub);
}

}
