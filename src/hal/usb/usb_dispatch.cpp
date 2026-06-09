// usb_dispatch.cpp — Command parsing + per-command dispatch for the USB CDC
// layer. Carved out of usb_comm.cpp during the #1207 refactor.
//
// Scope: every command handler that is NOT typed-config sync. The typed
// device.json / input_bindings.json handlers live in usb_config_sync.cpp;
// transport-level concerns (sink fan-out, telemetry emit, CAN scan queue
// drain, host-activity tracking) stay in usb_comm.cpp.
//
//   CMD_PUT_CONFIG       0x02  -> handlePutConfig (dashboard.json + reboot)
//   CMD_GET_DEVICE_CONFIG 0x03 -> routes to UsbCommInternal::sendTypedConfigGet
//   CMD_PUT_DEVICE_CONFIG 0x04 -> routes to UsbCommInternal::handlePutDeviceConfig
//   CMD_SCREEN_SETTINGS  0x05  -> handleScreenSettings (brightness + rotation)
//   CMD_PUT_FILE         0x06  -> handlePutFile (chunked base64 upload)
//   CMD_TOGGLE_DAY_NIGHT 0x07  -> queue PendingActions::dayNightToggle
//   CMD_CALIBRATE_TOUCH  0x08  -> queue PendingActions::touchCalibrate
//   CMD_SET_DAY_NIGHT    0x09  -> queue PendingActions::dayNightSet
//   CMD_RESET_TOUCH_CAL  0x0A  -> queue PendingActions::touchCalibrationReset
//   CMD_GET_INPUT_BINDINGS 0x0B -> routes to UsbCommInternal::sendTypedConfigGet
//   CMD_PUT_INPUT_BINDINGS 0x0C -> routes to UsbCommInternal::handlePutInputBindings
//   CMD_GET_STATUS       0x10  -> firmware version / protocol / theme
//   CMD_GET_CONFIG       0x01  -> handleGetConfig (streams dashboard.json)
//   CMD_CAN_SCAN_START   0x20  -> allocate + arm the scan queue
//   CMD_CAN_SCAN_STOP    0x21  -> disarm + free the scan queue

#include "usb_comm_internal.h"
#include "usb_envelope.h"

#include "app_config.h"
#include "board_config.h"
#include "can/signal_map.h"
#include "config/json_reader.h"
#include "config/rotation_config.h"
#include "diag/error_store.h"
#include "diag/logger.h"
#include "diag/lvgl_lock_guard.h"
#include "hal/storage/storage_driver.h"
#include "runtime/pending_actions.h"
#include "ui/settings_page.h"
#include "ui/theme_manager.h"

#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <mbedtls/base64.h>

#ifdef ARDUINO
    #include <Arduino.h>
#endif

#include <atomic>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Forward-declared FreeRTOS LVGL mutex from main.cpp
extern SemaphoreHandle_t g_lvglMutex;

namespace {

// ---------------------------------------------------------------------------
// CAN scan queue — owned by the dispatch module because the queue lifecycle
// is driven entirely by the CMD_CAN_SCAN_START / CMD_CAN_SCAN_STOP handlers.
// The CAN task accesses the handle through UsbComm::pushCanFrame() (defined
// in usb_comm.cpp) and the USB task drains it through drainCanScanQueue()
// (also in usb_comm.cpp). The handle + portMUX live here; the transport
// layer reaches in through the accessors below.
// ---------------------------------------------------------------------------

// When scan mode is active, raw CAN frames are queued here by the CAN task
// (core 0) and drained by the USB task (core 1) in tick().
// Queue depth: 64 frames — sufficient for ~1s of bus traffic before dropping.
//
// Allocated lazily on CMD_CAN_SCAN_START, freed on CMD_CAN_SCAN_STOP. Boot
// init() leaves it null so we don't permanently hold ~1 KB DRAM for a feature
// that's rarely used (#976). All consumers null-check before touching.
//
// Teardown ordering (#1009): the CAN task on core 0 reads the handle and
// calls xQueueSend; the USB task on core 1 may concurrently free that handle
// on CMD_CAN_SCAN_STOP. A portMUX spinlock serialises every load/store of
// the handle so the swap to nullptr by the USB task and the load by the CAN
// task linearise — the CAN task either captures the live handle or sees
// nullptr and bails. The previous yield-and-hope patch in PR #1022 used
// vTaskDelay(1) which is only probabilistic under bus load; the lock makes
// the handle-access invariant deterministic.
constexpr uint8_t CAN_SCAN_QUEUE_DEPTH = 64;
QueueHandle_t s_canScanQueue = nullptr;
portMUX_TYPE s_canScanQueueMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool s_canScanMode = false;

// Count of frames dropped due to a full scan queue since the last scan start.
uint32_t s_scanDrops = 0;

// ---------------------------------------------------------------------------
// CMD_PUT_FILE — chunked, base64-encoded file write to storage
// ---------------------------------------------------------------------------

constexpr uint32_t CHUNK_TIMEOUT_MS = 10000;

struct ChunkTransfer {
    char path[CFG_MAX_PATH_LEN];
    uint32_t expectedIdx;
    uint32_t total;
    uint32_t lastActivityMs;
};
ChunkTransfer s_chunk = {{0}, 0, 0, 0};

void abortChunkTransfer(const char *reason) {
    if (StorageDriver::isChunkedWriteOpen()) {
        LOG_WARN("USB", "Aborting chunked transfer: %s (path=%s)", reason, s_chunk.path);
        StorageDriver::abortChunkedWrite();
    }
    s_chunk.path[0] = '\0';
    s_chunk.expectedIdx = 0;
    s_chunk.total = 0;
}

// Allowlist of top-level prefixes the host may target via CMD_PUT_FILE.
// /config/ is intentionally absent — that path is owned by CMD_PUT_CONFIG,
// which validates against the dashboard schema. Add new prefixes here only
// after reviewing the impact on the storage layout.
const char *kAllowedPutPrefixes[] = {
    "/assets/",
    "/fonts/",
};

bool isPathSafe(const char *path) {
    if (!path)
        return false;
    const size_t len = strnlen(path, CFG_MAX_PATH_LEN);
    if (len == 0 || len >= CFG_MAX_PATH_LEN)
        return false;

    // Reject control chars (0x00..0x1F) and DEL (0x7F). NUL would already
    // truncate strnlen, but checking here documents intent.
    for (size_t i = 0; i < len; ++i) {
        const unsigned char c = static_cast<unsigned char>(path[i]);
        if (c < 0x20 || c == 0x7F)
            return false;
    }

    // Reject double slashes — would let a host obfuscate the prefix match
    // ("/assets//../config/x" style payloads on hosts that normalize lazily).
    if (strstr(path, "//"))
        return false;

    // Belt-and-braces: the anchored allowlist below already prevents traversal
    // on flat SPIFFS, but reject literal ".." to keep this defense-in-depth.
    if (strstr(path, ".."))
        return false;

    // Require an exact prefix match from the allowlist. The path must contain
    // at least one byte beyond the prefix (i.e. an actual filename).
    for (const char *prefix : kAllowedPutPrefixes) {
        const size_t plen = strlen(prefix);
        if (len > plen && strncmp(path, prefix, plen) == 0)
            return true;
    }
    return false;
}

void handlePutFile(const JsonObjectConst &obj) {
    const char *path = obj["path"];
    const uint32_t total = obj["total"] | 0u;
    const uint32_t idx = obj["idx"] | 0u;
    const char *b64 = obj["data"];

    if (!isPathSafe(path) || !b64 || total == 0 || idx >= total) {
        UsbComm::sendLine("{\"status\":\"error\",\"message\":\"bad_args\"}");
        abortChunkTransfer("bad_args");
        return;
    }

    if (idx == 0) {
        abortChunkTransfer("new transfer");
        if (!StorageDriver::beginChunkedWriteAtomic(path)) {
            UsbComm::sendLine("{\"status\":\"error\",\"message\":\"open_failed\"}");
            return;
        }
        strlcpy(s_chunk.path, path, sizeof(s_chunk.path));
        s_chunk.total = total;
        s_chunk.expectedIdx = 0;
    } else if (!StorageDriver::isChunkedWriteOpen() || s_chunk.expectedIdx != idx ||
               strcmp(s_chunk.path, path) != 0) {
        UsbComm::sendLine("{\"status\":\"error\",\"message\":\"out_of_sequence\"}");
        abortChunkTransfer("out_of_sequence");
        return;
    }

    // Decode base64 in-place into s_rxBuf — ArduinoJson 7 has already copied
    // path/data into the JsonDocument's pool, so the source line is free.
    size_t decoded = 0;
    const int rc = mbedtls_base64_decode(
        reinterpret_cast<unsigned char *>(UsbCommInternal::s_rxBuf), USB_RX_BUF_SIZE, &decoded,
        reinterpret_cast<const unsigned char *>(b64), strlen(b64));
    if (rc != 0) {
        UsbComm::sendLine("{\"status\":\"error\",\"message\":\"b64_decode\"}");
        abortChunkTransfer("b64_decode");
        return;
    }

    if (!StorageDriver::appendChunk(reinterpret_cast<const uint8_t *>(UsbCommInternal::s_rxBuf),
                                    decoded)) {
        UsbComm::sendLine("{\"status\":\"error\",\"message\":\"write_failed\"}");
        abortChunkTransfer("write_failed");
        return;
    }

    s_chunk.expectedIdx = idx + 1;
    s_chunk.lastActivityMs = millis();

    if (idx == total - 1) {
        const bool finalized = StorageDriver::endChunkedWrite();
        if (!finalized) {
            UsbComm::sendLine("{\"status\":\"error\",\"message\":\"write_failed\"}");
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

    UsbComm::sendLine("{\"status\":\"ok\"}");
}

// ---------------------------------------------------------------------------
// CMD_PUT_CONFIG (0x02): write new dashboard.json to storage, then reboot.
// See the legacy implementation comments preserved below for the LVGL mutex
// and priority-boost rationale.
// ---------------------------------------------------------------------------

// Handle CMD_PUT_CONFIG (0x02): write new dashboard.json to storage, then
// reboot via esp_restart(). Hot reload (PageManager::requestReload) is not
// viable at runtime: LVGL's 80 KB heap pool leaves only ~2 KB of contiguous
// DRAM free, so ArduinoJson fails with NoMemory on any real config file
// (issue #576 fallout). Cold boot starts with ~110 KB free — enough to parse
// the JSON before LVGL is initialised. Studio already shows a "reconnect"
// spinner after PUT_CONFIG, so the UX is identical.
//
// Implementation note: we do NOT parse the envelope into a JsonDocument.
// At schema v1.13 the dashboard JSON is ~13 KB compact; ArduinoJson v7 grows
// its pool to ~21 KB to materialise it, and a contiguous 21 KB malloc fails
// once LVGL has consumed its 80 KB pool. Instead, locate the `"payload"`
// value as a substring of s_rxBuf and stream it straight to flash.
//
// Race note: the SPIFFS/IDF flash driver yields to the OS between page writes
// (vTaskDelay(0)). When USB yields, the LVGL task runs, calls lv_task_handler(),
// lv_mem_alloc() fails (~1 KB free in LVGL heap), OOM assert fires esp_restart()
// mid-write — config never committed. A priority boost alone does NOT prevent
// this because vTaskDelay(0) is an explicit yield regardless of priority.
// Fix: hold g_lvglMutex for the entire write so lv_task_handler() is blocked.
// LVGL ticks that fire during the write see "mutex timeout" and skip — harmless
// since the device reboots immediately after. On failure the mutex is released
// so the UI recovers.
//
// Overlay paint (#1207 #1314 batch-render): the BurnOverlay is no longer drawn
// inline on this (USB) task. invokeBurnOverlayShow() raises a pending-action
// flag and notifies the UI task; the UI task paints the overlay at TASK_PRIO_UI
// inside its own LVGL-mutex window — strictly BEFORE we acquire the mutex here
// because the UI task wakes from xTaskNotify and acquires the mutex first
// (TASK_PRIO_UI=10 preempts TASK_PRIO_USB=8 on core 1). The small grace delay
// gives the UI tick room to paint + flush before this task blocks the mutex
// for the duration of the flash write. This replaces the previous
// vTaskPrioritySet(TASK_PRIO_UI+1) boost which starved equal-priority core-1
// tasks (BLE/WiFi) during the write.
constexpr uint32_t BURN_OVERLAY_RENDER_GRACE_MS = 20;

void handlePutConfig(const char *jsonLine) {
#ifdef ARDUINO
    LOG_INFO("USB", "heap.largest_free=%u before PUT_CONFIG",
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
#endif

    const size_t lineLen = strlen(jsonLine);
    size_t written = 0;
    const char *payloadStart = UsbEnvelope::findPayloadSlice(jsonLine, lineLen, &written);
    if (!payloadStart || written == 0) {
        LOG_WARN("USB", "PUT_CONFIG: missing or malformed payload field");
        UsbComm::sendLine("{\"status\":\"error\",\"message\":\"missing_payload\"}");
        return;
    }

    // Request the BurnOverlay spinner through the registered observer. The
    // observer sets PendingActions::burnOverlayShow and notifies the UI task
    // so it wakes from its idle wait and paints at TASK_PRIO_UI under the
    // LVGL mutex. No-op until main.cpp::setup() has registered the callback.
    UsbCommInternal::invokeBurnOverlayShow();

    // Yield so the UI task — at TASK_PRIO_UI (10) > TASK_PRIO_USB (8) on the
    // same core — preempts and runs one frame: drain burnOverlayShow,
    // BurnOverlay::show() under g_lvglMutex (build + lv_refr_now), release
    // mutex. After this delay the overlay is on screen and the mutex is free
    // for us to take. Tuned to one LVGL_HANDLER_PERIOD_MS plus a safety
    // margin so even a slightly delayed UI tick still completes.
    vTaskDelay(pdMS_TO_TICKS(BURN_OVERLAY_RENDER_GRACE_MS));

    // Take the LVGL mutex before the write. This blocks lv_task_handler() for
    // the entire duration so the OOM assert cannot fire mid-write. No
    // priority flip — running at TASK_PRIO_USB keeps core-1 schedulable for
    // BLE/WiFi while the flash write proceeds.
    //
    // Mutex contention is treated as an explicit failure (#1337): the prior
    // "proceed without the mutex" fallback let `lv_task_handler` race a
    // concurrent LVGL allocation against the flash write, which could
    // OOM-panic mid-`writeFileAtomic`. `writeFileAtomic` already protects the
    // live config via `.tmp` rename, but a panic leaves the user with a
    // silent failure and a stale `.tmp` until the boot-time sweep clears it.
    if (xSemaphoreTake(g_lvglMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_WARN("USB", "PUT_CONFIG: LVGL mutex busy — aborting write");
        UsbCommInternal::invokeBurnOverlayShowError(0);
        UsbComm::sendLine("{\"status\":\"error\",\"message\":\"busy\"}");
        return;
    }

    bool ok = StorageDriver::writeFileAtomic(
        CONFIG_PATH_DASHBOARD, reinterpret_cast<const uint8_t *>(payloadStart), written);
    if (!ok) {
        xSemaphoreGive(g_lvglMutex);
        // Reason code 0 = BurnOverlay::ErrorReason::WriteFailed. Defined in
        // ui/burn_overlay.h; mirrored as int through the observer to keep
        // the USB module free of any LVGL-side includes.
        UsbCommInternal::invokeBurnOverlayShowError(0);
        LOG_ERROR("USB", "PUT_CONFIG: storage write failed");
        UsbComm::sendLine("{\"status\":\"error\",\"message\":\"write_failed\"}");
        return;
    }

    LOG_INFO("USB", "PUT_CONFIG: dashboard.json updated (%u bytes) — rebooting", written);
    UsbComm::sendLine("{\"status\":\"ok\",\"rebooting\":true}");
    Serial.flush();
    esp_restart(); // never returns
}

// ---------------------------------------------------------------------------
// CMD_SCREEN_SETTINGS (0x05)
// ---------------------------------------------------------------------------

void handleScreenSettings(const JsonObjectConst &obj) {
    uint8_t brightness = obj["brightness"] | 80;

    if (xSemaphoreTake(g_lvglMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        LVGL_HOLD_GUARD(::PerfCounters::MUTEX_HOLD_USB);
        SettingsPage::applyFromUsb(brightness);
        xSemaphoreGive(g_lvglMutex);
    } else {
        LOG_WARN("USB", "Screen settings: could not acquire LVGL mutex");
        UsbComm::sendLine("{\"status\":\"error\",\"message\":\"busy\"}");
        return;
    }

    // Mounting rotation override — only applied if the value actually changed,
    // since applying it triggers a reboot.
    JsonVariantConst rotationVar = obj["rotation"];
    if (!rotationVar.isNull()) {
        const uint16_t rotation = rotationVar.as<uint16_t>();
        if ((rotation == 0 || rotation == 180) && rotation != RotationConfig::getOffsetDeg()) {
            LOG_INFO("USB", "Rotation change requested: %u° — rebooting", rotation);
            UsbComm::sendLine("{\"status\":\"ok\",\"rebooting\":true}");
            Serial.flush();
            RotationConfig::applyAndReboot(rotation); // never returns
        }
    }

    UsbComm::sendLine("{\"status\":\"ok\"}");
}

// ---------------------------------------------------------------------------
// CMD_GET_CONFIG — assemble {"status":"ok","config":<dashboard.json>} into a
// single heap buffer and emit through `UsbComm::sendLine`, so every active
// sink (USB-CDC / TCP / WS) receives the reply (#1123 follow-up).
//
// The pre-fix path used three raw `Serial.print` writes, bypassing the sink
// fan-out added in #1073 — dash-hosted Studio clients on WS therefore saw
// nothing on CMD_GET_CONFIG. The wire format is unchanged (still
// `{"status":"ok","config":<json>}`) so the existing USB-CDC Studio (which
// already parses the same envelope) keeps working.
//
// Memory: prefer PSRAM (16 KB is cheap there, keeps internal DRAM available
// for LVGL / WiFi). Fall back to internal DRAM on no-PSRAM boards; abort
// with an `oom` error rather than partially streaming and breaking framing.
// Newlines in the source file are mapped to spaces inline (JSON treats them
// as whitespace anyway) so the assembled response stays one logical line.
// ---------------------------------------------------------------------------

// Print adapter that copies bytes into a fixed-capacity heap buffer. Reused by
// handleGetConfig() so streamFileTo() can fill the envelope's body slot in
// place without a second stack-staging buffer.
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

void handleGetConfig() {
    if (!StorageDriver::fileExists(CONFIG_PATH_DASHBOARD)) {
        UsbComm::sendLine("{\"status\":\"error\",\"message\":\"config_not_found\"}");
        return;
    }
    const size_t fileBytes = StorageDriver::fileSize(CONFIG_PATH_DASHBOARD);
    if (fileBytes == 0) {
        LOG_WARN("USB", "GET_CONFIG: empty / unreadable %s", CONFIG_PATH_DASHBOARD);
        UsbComm::sendLine("{\"status\":\"error\",\"message\":\"config_not_found\"}");
        return;
    }
    // Envelope is `{"status":"ok","config":` (24 B) + body + `}` (1 B) + NUL.
    // Pad a few extra bytes for safety.
    static constexpr size_t kEnvelopeOverhead = 32;
    const size_t needed = fileBytes + kEnvelopeOverhead;
    char *buf = static_cast<char *>(heap_caps_malloc(needed, MALLOC_CAP_SPIRAM));
    if (!buf) {
        buf = static_cast<char *>(heap_caps_malloc(needed, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
    }
    if (!buf) {
        LOG_ERROR("USB", "GET_CONFIG: response buffer alloc (%u B) failed",
                  static_cast<unsigned>(needed));
        UsbComm::sendLine("{\"status\":\"error\",\"message\":\"oom\"}");
        return;
    }
    static const char kPrefix[] = "{\"status\":\"ok\",\"config\":";
    constexpr size_t kPrefixLen = sizeof(kPrefix) - 1;
    memcpy(buf, kPrefix, kPrefixLen);

    BufferPrint sink(buf + kPrefixLen, needed - kPrefixLen - 2 /* '}' + '\0' */);
    const size_t streamed = StorageDriver::streamFileTo(CONFIG_PATH_DASHBOARD, sink,
                                                        true /* replaceNewlinesWithSpaces */);
    // Detect a mid-flight streamFileTo failure (read error after the size check
    // passed). Composing the envelope with an empty body would ship
    // `{"status":"ok","config":}` — invalid JSON to the host. Emit the canonical
    // error envelope instead. Issue #1286.
    //
    // Also detect a partial fill (`streamed != sink.used()`): `BufferPrint::write`
    // silently caps at its capacity, so a stream that returns more bytes than
    // the sink absorbed means the body is truncated. Shipping
    // `{"status":"ok","config":<truncated>}` would silently corrupt the host's
    // view of the device config (#1337).
    if (streamed == 0 || sink.used() == 0 || streamed != sink.used()) {
        free(buf);
        LOG_WARN("USB", "GET_CONFIG: stream mismatch for %s (streamed=%u, sink=%u)",
                 CONFIG_PATH_DASHBOARD, static_cast<unsigned>(streamed),
                 static_cast<unsigned>(sink.used()));
        UsbComm::sendLine("{\"status\":\"error\",\"message\":\"config_read_failed\"}");
        return;
    }
    size_t pos = kPrefixLen + sink.used();
    buf[pos++] = '}';
    buf[pos] = '\0';
    UsbComm::sendLine(buf);
    free(buf);

    LOG_INFO("USB", "GET_CONFIG: sent %u bytes", static_cast<unsigned>(streamed));
}

} // namespace

namespace UsbCommInternal {

// ---------------------------------------------------------------------------
// Public-to-transport accessors for the CAN scan queue. usb_comm.cpp uses
// these from the CAN task (pushCanFrame) and the USB task drain
// (drainCanScanQueue). Kept thin so the queue lifecycle stays owned by the
// dispatch module.
// ---------------------------------------------------------------------------

bool canScanModeActive() {
    return s_canScanMode;
}

bool canScanQueueTrySend(const UsbComm::CanScanFrame &frame) {
    BaseType_t sent = pdFAIL;
    portENTER_CRITICAL(&s_canScanQueueMux);
    const bool hasQueue = (s_canScanQueue != nullptr);
    if (hasQueue) {
        sent = xQueueSend(s_canScanQueue, &frame, 0);
    }
    portEXIT_CRITICAL(&s_canScanQueueMux);
    if (!hasQueue)
        return false;
    if (sent != pdTRUE) {
        s_scanDrops++;
        return false;
    }
    return true;
}

bool canScanQueueTryReceive(UsbComm::CanScanFrame &out) {
    // Mirror the critical-section discipline of `canScanQueueTrySend` (#1042):
    // `CMD_CAN_SCAN_STOP` nulls `s_canScanQueue` and `vQueueDelete`s the
    // handle on the USB task while the CAN task can be between the null check
    // and the `xQueueReceive` here. Without the mux the receive path races
    // on a freed queue handle — use-after-free under cancel-mid-scan (#1336).
    BaseType_t received = pdFAIL;
    portENTER_CRITICAL(&s_canScanQueueMux);
    if (s_canScanQueue != nullptr) {
        received = xQueueReceive(s_canScanQueue, &out, 0);
    }
    portEXIT_CRITICAL(&s_canScanQueueMux);
    return received == pdTRUE;
}

void tickChunkTransferTimeout() {
    if (StorageDriver::isChunkedWriteOpen() &&
        (millis() - s_chunk.lastActivityMs) > CHUNK_TIMEOUT_MS) {
        abortChunkTransfer("idle timeout");
    }
}

void handleCommand(const char *jsonLine) {
    s_lastHostCmdMs = millis();
    LOG_VDEBUG("USB", "Received command: %.40s...", jsonLine);

    // Funnel every parse through JsonReader so the binary keeps a single
    // BoundedReader<const char*> instantiation (#406). strlen runs once and
    // the value is reused for the filter peek + the full parse below.
    const size_t jsonLen = strlen(jsonLine);

    // Peek at cmd using a filter — avoids loading the full PUT_CONFIG payload
    JsonDocument cmdFilter;
    cmdFilter["cmd"] = true;
    JsonDocument peekDoc;
    JsonReader::parseFiltered(peekDoc, jsonLine, jsonLen, cmdFilter);
    uint8_t cmd = peekDoc["cmd"] | 0;

    if (cmd == UsbComm::CMD_PUT_CONFIG) {
        handlePutConfig(jsonLine);
        return;
    }

    if (cmd == UsbComm::CMD_PUT_FILE) {
        // Parse with a filter that drops everything except the fields we use —
        // each chunk's "data" (base64) can be up to ~3 KB, so we want the doc
        // sized to that range, not the full 6 KB s_rxBuf.
        JsonDocument doc;
        DeserializationError err = JsonReader::parse(doc, jsonLine, jsonLen);
        if (err) {
            LOG_WARN("USB", "PUT_FILE parse error: %s", err.c_str());
            UsbComm::sendLine("{\"status\":\"error\",\"message\":\"parse_error\"}");
            abortChunkTransfer("parse_error");
            return;
        }
        handlePutFile(doc.as<JsonObjectConst>());
        return;
    }

    JsonDocument doc;
    DeserializationError err = JsonReader::parse(doc, jsonLine, jsonLen);
    if (err) {
        LOG_WARN("USB", "JSON parse error: %s", err.c_str());
        UsbComm::sendLine("{\"status\":\"error\",\"message\":\"parse_error\"}");
        return;
    }

    switch (cmd) {
        case UsbComm::CMD_GET_STATUS: {
            char resp[160];
            // snprintf returns the would-have-written length — if it equals or
            // exceeds sizeof(resp), the payload is truncated and no longer
            // valid JSON. Log + skip so the host doesn't get garbage (#936).
            const int n =
                snprintf(resp, sizeof(resp),
                         "{\"status\":\"ok\",\"version\":\"%s\",\"protocol\":%u,\"is_day\":%d}",
                         APP_VERSION_STR, static_cast<unsigned>(USB_PROTOCOL_VERSION),
                         ThemeManager::isDayMode() ? 1 : 0);
            if (n <= 0 || static_cast<size_t>(n) >= sizeof(resp)) {
                LOG_WARN("USB", "GET_STATUS payload truncated (n=%d, cap=%u)", n,
                         static_cast<unsigned>(sizeof(resp)));
                break;
            }
            UsbComm::sendLine(resp);
            break;
        }
        case UsbComm::CMD_PING: {
            char resp[48];
            const int n = snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"uptime_ms\":%lu}",
                                   static_cast<unsigned long>(millis()));
            if (n > 0 && static_cast<size_t>(n) < sizeof(resp)) {
                UsbComm::sendLine(resp);
            }
            break;
        }
        case UsbComm::CMD_GET_CONFIG:
            handleGetConfig();
            break;
        case UsbComm::CMD_GET_DEVICE_CONFIG:
            // device.json on disk is already the flat snake_case shape the
            // wire schema expects — pass it through unwrapped.
            sendTypedConfigGet(CONFIG_PATH_DEVICE, "device_config", nullptr);
            break;
        case UsbComm::CMD_PUT_DEVICE_CONFIG:
            handlePutDeviceConfig(doc.as<JsonObjectConst>());
            break;
        case UsbComm::CMD_GET_INPUT_BINDINGS:
            // input_bindings.json wraps the array as `{"input_bindings":[...]}`
            // — lift the inner array so the wire envelope carries the bare
            // array under the same key (matches InputBindingsConfigWireSchema).
            sendTypedConfigGet(CONFIG_PATH_INPUTS, "input_bindings", "input_bindings");
            break;
        case UsbComm::CMD_PUT_INPUT_BINDINGS:
            handlePutInputBindings(doc.as<JsonObjectConst>());
            break;
        case UsbComm::CMD_SCREEN_SETTINGS:
            handleScreenSettings(doc.as<JsonObjectConst>());
            break;
        case UsbComm::CMD_TOGGLE_DAY_NIGHT:
            PendingActions::dayNightToggle.store(true, std::memory_order_relaxed);
            LOG_INFO("USB", "CMD: day/night toggle queued");
            UsbComm::sendLine("{\"status\":\"ok\"}");
            break;
        case UsbComm::CMD_SET_DAY_NIGHT: {
            JsonVariantConst dayVar = doc["day"];
            if (dayVar.isNull() || !dayVar.is<bool>()) {
                LOG_WARN("USB", "set_day_night missing 'day' bool");
                UsbComm::sendLine("{\"status\":\"error\",\"message\":\"missing_day\"}");
                break;
            }
            const bool day = dayVar.as<bool>();
            PendingActions::dayNightSet.store(day ? 1 : 0, std::memory_order_relaxed);
            LOG_INFO("USB", "CMD: day/night set queued — %s", day ? "day" : "night");
            UsbComm::sendLine("{\"status\":\"ok\"}");
            break;
        }
        case UsbComm::CMD_CALIBRATE_TOUCH:
            PendingActions::touchCalibrate.store(true, std::memory_order_relaxed);
            LOG_INFO("USB", "CMD: calibration queued");
            UsbComm::sendLine("{\"status\":\"ok\"}");
            break;
        case UsbComm::CMD_RESET_TOUCH_CAL:
            PendingActions::touchCalibrationReset.store(true, std::memory_order_relaxed);
            LOG_INFO("USB", "CMD: calibration reset queued");
            UsbComm::sendLine("{\"status\":\"ok\"}");
            break;
        case UsbComm::CMD_CAN_SCAN_START: {
            s_scanDrops = 0;
            // Allocate outside the critical section — xQueueCreate may take
            // the heap mutex and must never run with interrupts disabled.
            QueueHandle_t fresh = nullptr;
            if (!s_canScanQueue) {
                fresh = xQueueCreate(CAN_SCAN_QUEUE_DEPTH, sizeof(UsbComm::CanScanFrame));
                if (!fresh) {
                    LOG_ERROR("USB", "CAN scan start: queue alloc failed");
#if APP_USB_CAN_SCAN_FAIL_LOUD
                    // #976 reproducer hook — abort here so a fresh boot with
                    // the queue present is easy to distinguish from one that
                    // already started in a degraded state. Off in release.
                    abort();
#endif
                    // Surface to the on-screen error badge so a failed scan
                    // start is not silent past the USB reply (F-HI-7).
                    ErrorStore::push(ERROR_SRC_SYSTEM, "SCAN_QUEUE", "CAN scan queue alloc failed");
                    UsbComm::sendLine("{\"status\":\"error\",\"error\":\"queue_alloc_failed\"}");
                    break;
                }
            }
            // Publish the new handle under the spinlock so the CAN task sees
            // a fully constructed queue before s_canScanMode flips true. Kept
            // symmetric with the STOP path even though the mode-gate dominates
            // here, so the invariant "handle access is always serialised" is
            // visibly upheld at every site.
            portENTER_CRITICAL(&s_canScanQueueMux);
            if (fresh) {
                s_canScanQueue = fresh;
            }
            QueueHandle_t toReset = s_canScanQueue;
            portEXIT_CRITICAL(&s_canScanQueueMux);
            xQueueReset(toReset);
            s_canScanMode = true;
            LOG_INFO("USB", "CAN scan started");
            UsbComm::sendLine("{\"status\":\"ok\"}");
            break;
        }
        case UsbComm::CMD_CAN_SCAN_STOP: {
            s_canScanMode = false;
            // Atomically detach the queue handle so the CAN task either sees
            // it (and finishes xQueueSend on a still-valid queue before we
            // can re-enter and free it) or sees nullptr (and bails). The
            // critical section disables interrupts on this core and prevents
            // the other core from entering its matching section, giving us
            // the deterministic ordering PR #1022's vTaskDelay only
            // approximated (#1009).
            QueueHandle_t toDelete;
            portENTER_CRITICAL(&s_canScanQueueMux);
            toDelete = s_canScanQueue;
            s_canScanQueue = nullptr;
            portEXIT_CRITICAL(&s_canScanQueueMux);
            // Free the queue back to the heap — scan mode is opt-in, the
            // expected steady state is no queue at all. Keeps ~1 KB
            // contiguous DRAM available for icon decodes / page rebuilds.
            if (toDelete) {
                vQueueDelete(toDelete);
            }
            LOG_INFO("USB", "CAN scan stopped — drops: %lu", (unsigned long)s_scanDrops);
            char stopResp[64];
            const int stopN =
                snprintf(stopResp, sizeof(stopResp), "{\"status\":\"ok\",\"drops\":%lu}",
                         (unsigned long)s_scanDrops);
            if (stopN <= 0 || static_cast<size_t>(stopN) >= sizeof(stopResp)) {
                LOG_WARN("USB", "STOP_CAN_SCAN payload truncated (n=%d, cap=%u)", stopN,
                         static_cast<unsigned>(sizeof(stopResp)));
                break;
            }
            UsbComm::sendLine(stopResp);
            break;
        }
        default:
            // Surface opcode drift loudly so a tuner built against a future
            // protocol can't silently fall through with a fake `ok`. Old
            // behaviour returned `{"status":"ok"}` here, which let the typed
            // envelope drift between tuner and firmware without any signal
            // until a downstream parser blew up (#1365).
            LOG_WARN("USB", "Unknown cmd: 0x%02X — replying unknown_command", cmd);
            UsbComm::sendLine("{\"status\":\"error\",\"message\":\"unknown_command\"}");
            break;
    }
}

} // namespace UsbCommInternal
