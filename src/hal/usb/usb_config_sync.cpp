
#include "usb_comm_internal.h"

#include "app_config.h"
#include "board_config.h"
#include "diag/logger.h"
#include "hal/storage/storage_driver.h"

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

void persistTypedConfigAndReboot(const char *path, const char *fieldKey,
                                 JsonVariantConst subValue) {
    JsonDocument out;
    out[fieldKey] = subValue;

    const size_t projected = measureJson(out);
    if (projected == 0 || projected > UsbCommInternal::kTypedPutMaxPayloadBytes) {
        LOG_WARN("USB", "PUT %s: serialized size %u out of bounds", path,
                 static_cast<unsigned>(projected));
        UsbComm::sendLine("{\"status\":\"error\",\"message\":\"too_large\"}");
        return;
    }

    if (!StorageDriver::beginChunkedWriteAtomic(path)) {
        LOG_ERROR("USB", "PUT %s: open failed", path);
        UsbComm::sendLine("{\"status\":\"error\",\"message\":\"write_failed\"}");
        return;
    }

    ChunkedAtomicWriter writer;
    const size_t written = serializeJson(out, writer);
    if (written != projected) {
        StorageDriver::abortChunkedWrite();
        LOG_ERROR("USB", "PUT %s: stream failed (%u/%u bytes)", path,
                  static_cast<unsigned>(written), static_cast<unsigned>(projected));
        UsbComm::sendLine("{\"status\":\"error\",\"message\":\"write_failed\"}");
        return;
    }

    if (!StorageDriver::endChunkedWrite()) {
        LOG_ERROR("USB", "PUT %s: commit failed", path);
        UsbComm::sendLine("{\"status\":\"error\",\"message\":\"write_failed\"}");
        return;
    }

    LOG_INFO("USB", "PUT %s: %u bytes written — rebooting", path, static_cast<unsigned>(written));
    UsbComm::sendLine("{\"status\":\"ok\",\"rebooting\":true}");
#ifdef ARDUINO
    Serial.flush();
    esp_restart();
#endif
}

} // namespace

namespace UsbCommInternal {

void sendTypedConfigGet(const char *path, const char *fieldKey, const char *unwrapKey) {
    if (!StorageDriver::fileExists(path)) {
        UsbComm::sendLine("{\"status\":\"error\",\"message\":\"config_not_found\"}");
        return;
    }

    JsonDocument fileDoc;
    DeserializationError err = StorageDriver::parseJsonFile(path, fileDoc);
    if (err) {
        LOG_WARN("USB", "GET %s parse error: %s", path, err.c_str());
        UsbComm::sendLine("{\"status\":\"error\",\"message\":\"parse_error\"}");
        return;
    }

    JsonVariantConst body =
        unwrapKey ? fileDoc[unwrapKey].as<JsonVariantConst>() : fileDoc.as<JsonVariantConst>();
    if (body.isNull()) {
        LOG_WARN("USB", "GET %s: missing '%s' in file body", path,
                 unwrapKey ? unwrapKey : "(root)");
        UsbComm::sendLine("{\"status\":\"error\",\"message\":\"config_not_found\"}");
        return;
    }

    JsonDocument resp;
    resp["status"] = "ok";
    resp[fieldKey] = body;

    const size_t needed = measureJson(resp) + 1;
    char *buf = static_cast<char *>(heap_caps_malloc(needed, MALLOC_CAP_SPIRAM));
    if (!buf) {
        buf = static_cast<char *>(heap_caps_malloc(needed, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
    }
    if (!buf) {
        LOG_ERROR("USB", "GET %s: response buffer alloc (%u B) failed", path,
                  static_cast<unsigned>(needed));
        UsbComm::sendLine("{\"status\":\"error\",\"message\":\"oom\"}");
        return;
    }
    const size_t written = serializeJson(resp, buf, needed);
    buf[written] = '\0';
    UsbComm::sendLine(buf);
    heap_caps_free(buf);
}

void handlePutDeviceConfig(const JsonObjectConst &obj) {
    JsonVariantConst sub = obj["device_config"];
    if (sub.isNull() || !sub.is<JsonObjectConst>()) {
        UsbComm::sendLine("{\"status\":\"error\",\"message\":\"missing_device_config\"}");
        return;
    }
    persistTypedConfigAndReboot(CONFIG_PATH_DEVICE, "device_config", sub);
}

void handlePutInputBindings(const JsonObjectConst &obj) {
    JsonVariantConst sub = obj["input_bindings"];
    if (sub.isNull() || !sub.is<JsonArrayConst>()) {
        UsbComm::sendLine("{\"status\":\"error\",\"message\":\"missing_input_bindings\"}");
        return;
    }
    persistTypedConfigAndReboot(CONFIG_PATH_INPUTS, "input_bindings", sub);
}

} // namespace UsbCommInternal
