// usb_config_sync.cpp — Typed config GET / PUT handlers for the USB CDC
// layer. Carved out of usb_comm.cpp during the #1207 refactor.
//
// Scope: device.json + input_bindings.json round-trips.
//
//   CMD_GET_DEVICE_CONFIG  / CMD_GET_INPUT_BINDINGS  -> sendTypedConfigGet
//   CMD_PUT_DEVICE_CONFIG                            -> handlePutDeviceConfig
//   CMD_PUT_INPUT_BINDINGS                           -> handlePutInputBindings
//
// CMD_GET_CONFIG (dashboard.json — large, sized at runtime against the file
// size) lives in usb_dispatch.cpp because it does not share the typed
// response-buffer sizing contract.
//
// Memory: typed GET / PUT handlers heap-allocate their staging buffer per
// request and free it before returning. The BSS pool from #1320 was reverted
// in #1332 because its 8 KB allocation pushed WROOM boot heap consumption
// past the budget needed for NimBLE / USB CDC / FreeRTOS object inits.
// See follow-up #1335 for re-attempt options (smaller pool or PSRAM-only).

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

// lockResponseBuffer / unlockResponseBuffer are vestigial stubs from the
// reverted #1320 BSS pool. Retained so the call sites stay diff-minimal
// against #1320's surface; future cleanup can inline them away once #1335
// settles on the re-attempt shape.
bool lockResponseBuffer() {
    return false;
}
void unlockResponseBuffer() {}

// Persist a typed-config payload to `path`, then reboot. The caller has
// already validated that `subValue` is non-null and matches the expected
// JSON shape (object for device.json, array for input_bindings.json). The
// stored file is `{"<fieldKey>": <subValue>}` so the boot-time loaders see
// the same wrapping shape they expect from a hand-edited file.
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

    // Heap-on-demand staging — the BSS pool was removed (see file header
    // comment); on this tight-DRAM board the heap-fallback path is the only
    // option. malloc/free per call accepts the fragmentation risk.
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
    esp_restart(); // never returns
#endif
}

} // namespace

namespace UsbCommInternal {

void initResponseBufferMutex() {
    // No-op since the BSS pool was removed — heap-fallback only on WROOM.
}

// Send a `{"status":"ok","<key>":<value>}` envelope by parsing the on-disk
// JSON file and lifting `unwrapKey` out of it (when non-null) before
// embedding the result under `fieldKey`. The unwrap is needed for
// input_bindings.json — on disk the file is `{"input_bindings":[...]}` but
// the wire envelope expects the bare array under the same key so the
// studio-side wire schema (`InputBindingsConfigWireSchema`) parses cleanly.
// For device.json the file body is already the flat shape the studio
// expects, so callers pass unwrapKey=nullptr to send it verbatim.
//
// On a missing file: sends `{"status":"error","message":"config_not_found"}`.
// Feeds the assembled response through `UsbComm::sendLine` so WS / TCP / USB
// sinks all receive it.
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

    const size_t needed = measureJson(resp) + 1; // payload + NUL terminator
    // Heap-on-demand staging — see file header comment for why the BSS pool
    // was removed.
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
