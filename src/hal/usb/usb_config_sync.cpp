// Typed GET/PUT for device.json + input_bindings.json. Dashboard.json (sized
// at runtime against file size) lives in usb_dispatch.cpp.
// BSS staging pool reverted in #1332 — heap-on-demand is the only option (#1335).
#include "usb_comm_internal.h"

#include "app_config.h"
#include "board_config.h"
#include "diag/logger.h"
#include "hal/storage/storage_driver.h"

#include <ArduinoJson.h>
#include <esp_system.h>

#ifdef ARDUINO
    #include <Arduino.h>
#endif

#include <stdlib.h>
#include <string.h>

namespace {

// Vestigial stubs from the reverted #1320 BSS pool — inline once #1335 lands.
bool lockResponseBuffer() {
    return false;
}
void unlockResponseBuffer() {}

void persistTypedConfigAndReboot(const char *path, const char *fieldKey,
                                 JsonVariantConst subValue) {
    JsonDocument out;
    out[fieldKey] = subValue;

    const size_t needed = measureJson(out);
    if (needed == 0 || needed > UsbCommInternal::kTypedPutMaxPayloadBytes) {
        LOG_WARN("USB", "PUT %s: serialized size %u out of bounds", path,
                 static_cast<unsigned>(needed));
        UsbComm::sendLine("{\"status\":\"error\",\"message\":\"too_large\"}");
        return;
    }

    uint8_t *buf = static_cast<uint8_t *>(malloc(needed));
    if (!buf) {
        LOG_ERROR("USB", "PUT %s: stage buffer alloc (%u B) failed", path,
                  static_cast<unsigned>(needed));
        UsbComm::sendLine("{\"status\":\"error\",\"message\":\"oom\"}");
        return;
    }
    const size_t written = serializeJson(out, buf, needed);
    const bool ok = StorageDriver::writeFileAtomic(path, buf, written);
    free(buf);

    if (!ok) {
        LOG_ERROR("USB", "PUT %s: storage write failed", path);
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

void initResponseBufferMutex() {}

// `unwrapKey` lifts the body out for input_bindings (disk wraps it under
// "input_bindings", wire envelope expects the bare array). device.json passes
// nullptr — its on-disk shape already matches the wire schema.
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
    char *buf = static_cast<char *>(malloc(needed));
    if (!buf) {
        LOG_ERROR("USB", "GET %s: response buffer alloc (%u B) failed", path,
                  static_cast<unsigned>(needed));
        UsbComm::sendLine("{\"status\":\"error\",\"message\":\"oom\"}");
        return;
    }
    const size_t written = serializeJson(resp, buf, needed);
    buf[written] = '\0';
    UsbComm::sendLine(buf);
    free(buf);
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
