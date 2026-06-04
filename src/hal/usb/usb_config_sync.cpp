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
// Memory: a module-static s_responseBuffer (8 KB, BSS) is reused by every
// typed handler so the hot path no longer does malloc/serializeJson/free per
// request (#1207 medium-perf entry). The mutex is created lazily by
// UsbCommInternal::initResponseBufferMutex(), invoked from UsbComm::init().

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

// Module-static response buffer reused by every typed GET / PUT handler so
// the hot path no longer does malloc/serializeJson/free per request — that
// pattern was fragmenting internal DRAM under sustained config traffic
// (#1207). Sized to kTypedPutMaxPayloadBytes (8 KB) which is the same cap
// the PUT path already enforces upstream. Lives in BSS; the cost is paid
// once at link time, not per request.
//
// Concurrency: a single FreeRTOS recursive mutex serialises buffer access
// across the USB task (sendTypedConfigGet / persistTypedConfigAndReboot
// called from handleCommand) and any future transport (TCP / WS) that
// routes typed config commands through the same handlers. The mutex is
// created via UsbCommInternal::initResponseBufferMutex() from
// UsbComm::init(). On lock-timeout the handler falls back to a one-shot
// heap alloc with an explicit log so a stuck transport can't drop a config
// reply silently — should be rare since each handler holds the lock for
// under 1 ms.
uint8_t s_responseBuffer[UsbCommInternal::kTypedPutMaxPayloadBytes];
SemaphoreHandle_t s_responseBufferMutex = nullptr;

// Bounded timeout mirrors the sink-mutex contract: long enough to absorb a
// concurrent handler's serialize+send pass (<1 ms typical) without
// indefinitely blocking a stuck caller.
constexpr TickType_t kResponseBufferLockTimeout = pdMS_TO_TICKS(50);

// Acquire / release the response buffer mutex. Returns true on success.
// Callers that get false fall back to a one-shot malloc.
bool lockResponseBuffer() {
    if (!s_responseBufferMutex)
        return false;
    return xSemaphoreTakeRecursive(s_responseBufferMutex, kResponseBufferLockTimeout) == pdTRUE;
}

void unlockResponseBuffer() {
    if (s_responseBufferMutex)
        xSemaphoreGiveRecursive(s_responseBufferMutex);
}

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

    bool ok = false;
    size_t written = 0;
    if (lockResponseBuffer()) {
        written = serializeJson(out, s_responseBuffer, sizeof(s_responseBuffer));
        ok = StorageDriver::writeFileAtomic(path, s_responseBuffer, written);
        unlockResponseBuffer();
    } else {
        // Fallback: mutex unavailable / contended. One-shot heap alloc keeps
        // the handler correct under contention; the warn lets us catch a
        // regression where this path stops being rare.
        LOG_WARN("USB", "PUT %s: response pool unavailable — heap fallback (%u B)", path,
                 static_cast<unsigned>(needed));
        uint8_t *buf = static_cast<uint8_t *>(malloc(needed));
        if (!buf) {
            LOG_ERROR("USB", "PUT %s: stage buffer alloc (%u B) failed", path,
                      static_cast<unsigned>(needed));
            UsbComm::sendLine("{\"status\":\"error\",\"message\":\"oom\"}");
            return;
        }
        written = serializeJson(out, buf, needed);
        ok = StorageDriver::writeFileAtomic(path, buf, written);
        free(buf);
    }

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
    if (s_responseBufferMutex)
        return;
    s_responseBufferMutex = xSemaphoreCreateRecursiveMutex();
    if (!s_responseBufferMutex) {
        // No fatal halt — typed GET/PUT handlers fall back to one-shot
        // heap allocs when the mutex is unavailable, restoring the
        // pre-#1207 behaviour. Logged so a recurring fallback is visible.
        LOG_WARN("USB", "Response buffer mutex alloc failed — typed config will heap-alloc");
    }
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
    if (needed <= sizeof(s_responseBuffer) && lockResponseBuffer()) {
        char *buf = reinterpret_cast<char *>(s_responseBuffer);
        const size_t written = serializeJson(resp, buf, sizeof(s_responseBuffer));
        buf[written] = '\0';
        UsbComm::sendLine(buf);
        unlockResponseBuffer();
        return;
    }

    // Fallback: payload larger than the pool, or mutex unavailable / contended.
    // Should be rare — log so we can spot a regression if it stops being rare.
    LOG_WARN("USB", "GET %s: response pool unavailable (need %u B, pool %u B) — heap fallback",
             path, static_cast<unsigned>(needed), static_cast<unsigned>(sizeof(s_responseBuffer)));
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
