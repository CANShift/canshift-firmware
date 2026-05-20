// usb_comm.cpp — USB serial communication layer
//
// Protocol: JSON lines over USB serial (UART0 / CP210x bridge), 115200 baud.
// Each message is one JSON object followed by \n.
//
// Desktop → device: {"cmd": <id>, ...fields}\n
// Device → desktop (command response): {"status":"ok"} or {"status":"error","message":"..."}\n
// Device → desktop (telemetry, proactive every ~200ms): {"tele":1,"v":{"rpm":...}}\n
//
// Static memory: only s_rxBuf (USB_RX_BUF_SIZE bytes, see app_config.h).
//   handlePutConfig reuses s_rxBuf for serialization after parsing — safe because
//   ArduinoJson 7 copies all values into the JsonDocument during parsing.

#include "usb_comm.h"
#include "board_config.h"
#include "app_config.h"
#include "diag/logger.h"
#include "diag/lvgl_lock_guard.h"
#include "hal/storage/storage_driver.h"
#include "config/config_loader.h"
#include "config/json_reader.h"
#include "config/rotation_config.h"
#include "ui/burn_overlay.h"
#include "ui/page_manager.h"
#include "ui/settings_page.h"
#include "ui/theme_manager.h"
#include "runtime/signal_store.h"
#include "runtime/pending_actions.h"
#include "can/signal_map.h"
#include "util/format_float.h"

#include <atomic>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <mbedtls/base64.h>

// Forward-declared FreeRTOS LVGL mutex from main.cpp
extern SemaphoreHandle_t g_lvglMutex;

// ---------------------------------------------------------------------------
// Telemetry signal table
// ---------------------------------------------------------------------------

namespace {

struct TeleEntry {
    SignalId id;
    const char *name;
};

// All signals exposed over USB telemetry (must match signals.json)
static const TeleEntry TELE_SIGNALS[] = {
    {SignalIds::RPM, "rpm"},
    {SignalIds::THROTTLE_POS, "throttle_pos"},
    {SignalIds::MAP_KPA, "map_kpa"},
    {SignalIds::BOOST_BAR, "boost_bar"},
    {SignalIds::IAT_C, "iat_c"},
    {SignalIds::COOLANT_TEMP_C, "coolant_temp_c"},
    {SignalIds::OIL_TEMP_C, "oil_temp_c"},
    {SignalIds::OIL_PRESS_BAR, "oil_press_bar"},
    {SignalIds::FUEL_PRESS_BAR, "fuel_press_bar"},
    {SignalIds::LAMBDA_1, "lambda_1"},
    {SignalIds::AFR_1, "afr_1"},
    {SignalIds::SPEED_KPH, "speed_kph"},
    {SignalIds::GEAR, "gear"},
    {SignalIds::BATTERY_VOLTS, "battery_volts"},
    {SignalIds::FLAG_MIL, "flag_mil"},
    {SignalIds::FLAG_LAUNCH_CTRL, "flag_launch_ctrl"},
    {SignalIds::MAP_NUMBER, "map_number"},
};

static constexpr size_t TELE_SIGNAL_COUNT = sizeof(TELE_SIGNALS) / sizeof(TELE_SIGNALS[0]);

// ---------------------------------------------------------------------------
// Receive state machine
// ---------------------------------------------------------------------------

// Single RX buffer — also reused as TX buffer in handlePutConfig after parsing.
// Size defined in app_config.h: CONFIG_JSON_DOC_DASHBOARD + 256.
static char s_rxBuf[USB_RX_BUF_SIZE];
static size_t s_rxPos = 0;

// Tick counter for telemetry scheduling (tick() runs every 20ms)
static uint8_t s_tickCount = 0;

// How many tick() calls between telemetry pushes: 10 × 20ms = 200ms
static constexpr uint8_t TELE_PERIOD_TICKS = 10;

// ---------------------------------------------------------------------------
// CAN scan queue
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
static constexpr uint8_t CAN_SCAN_QUEUE_DEPTH = 64;
static QueueHandle_t s_canScanQueue = nullptr;
static portMUX_TYPE s_canScanQueueMux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_canScanMode = false;

// Count of frames dropped due to a full scan queue since the last scan start.
static uint32_t s_scanDrops = 0;

// ---------------------------------------------------------------------------
// CAN health stats
// ---------------------------------------------------------------------------

// Written by CAN task (core 0), read by USB task (core 1).
// Volatile struct + pending flag — worst case: one stale display value.
struct CanHealthStats {
    uint32_t fpsX10;
    uint32_t errors;
};
static volatile CanHealthStats s_canStats = {0, 0};
static volatile bool s_canStatsPending = false;

// ---------------------------------------------------------------------------
// Telemetry
// ---------------------------------------------------------------------------

// Telemetry buffer lives on the stack (768 B, fits in USB task 4096 B stack).
// Format: {"tele":1,"v":{"rpm":1234.5,...}}\n
// Only valid (non-timed-out) signals are included.
static constexpr size_t TELE_BUF_SIZE = 768;

void sendTelemetry() {
    char buf[TELE_BUF_SIZE];
    char *p = buf;
    const char *end = buf + TELE_BUF_SIZE - 2; // reserve \n\0

    const char *prefix = "{\"tele\":1,\"v\":{";
    size_t prefixLen = strlen(prefix);
    if (p + prefixLen >= end)
        return;
    memcpy(p, prefix, prefixLen);
    p += prefixLen;

    bool first = true;
    for (size_t i = 0; i < TELE_SIGNAL_COUNT; i++) {
        if (!SignalStore::isValid(TELE_SIGNALS[i].id))
            continue;
        float val = SignalStore::read(TELE_SIGNALS[i].id);

        if (!first) {
            if (p >= end)
                break;
            *p++ = ',';
        }
        first = false;

        // Compose `"<name>":<number>` without `%f`/`%g` so the firmware can
        // drop newlib's float printf family. FloatFormat::formatGeneral mimics
        // `%.3g` (significant digits, trailing zeros stripped).
        int n = snprintf(p, static_cast<size_t>(end - p), "\"%s\":", TELE_SIGNALS[i].name);
        if (n <= 0 || p + n >= end)
            break;
        p += n;
        char numBuf[16];
        size_t numLen = FloatFormat::formatGeneral(numBuf, sizeof(numBuf), val, 3);
        if (numLen == 0 || p + numLen >= end)
            break;
        memcpy(p, numBuf, numLen);
        p += numLen;
    }

    if (p + 3 <= end) {
        *p++ = '}';
        *p++ = '}';
        *p++ = '\n';
        *p = '\0';
        UsbComm::sendLine(buf);
    }
}

// ---------------------------------------------------------------------------
// Command handlers
// ---------------------------------------------------------------------------

// Locate the value substring of the "payload" key inside a top-level JSON
// object. Returns a pointer to the first byte of the value (an opening '{')
// and writes the value length (including the closing '}') into outLen.
// Returns nullptr when the envelope is malformed or the key is missing.
//
// Why this lives here instead of using ArduinoJson: parsing a 12 KB envelope
// into a JsonDocument grew the pool to ~21 KB, which couldn't be satisfied
// after the LV_MEM_SIZE bump in #555 (issue #576). Skipping the full parse
// keeps the PUT_CONFIG path heap-allocation-free.
// Length-bounded substring search. Used in place of strstr() so an embedded
// NUL in the JSON line (corrupted USB stream, malformed input) cannot
// short-circuit the search before reaching the needle. Issue #884.
const char *findNeedle(const char *haystack, size_t haystackLen, const char *needle,
                       size_t needleLen) {
    if (needleLen == 0 || haystackLen < needleLen)
        return nullptr;
    const size_t lastStart = haystackLen - needleLen;
    for (size_t i = 0; i <= lastStart; ++i) {
        if (memcmp(haystack + i, needle, needleLen) == 0)
            return haystack + i;
    }
    return nullptr;
}

const char *findPayloadSlice(const char *jsonLine, size_t lineLen, size_t *outLen) {
    if (!jsonLine || !outLen)
        return nullptr;
    *outLen = 0;

    // Plain-text needle is acceptable here: the studio always emits the
    // envelope with the literal key `"payload"` and never inside a nested
    // string by accident — the surrounding `{"cmd":2,...}` frame is fixed.
    static constexpr char kNeedle[] = "\"payload\"";
    static constexpr size_t kNeedleLen = sizeof(kNeedle) - 1;
    const char *needle = findNeedle(jsonLine, lineLen, kNeedle, kNeedleLen);
    if (!needle)
        return nullptr;

    // Skip past `"payload"` then optional whitespace then the `:` separator.
    const char *cursor = needle + kNeedleLen;
    const char *end = jsonLine + lineLen;
    while (cursor < end &&
           (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r'))
        ++cursor;
    if (cursor >= end || *cursor != ':')
        return nullptr;
    ++cursor;
    while (cursor < end &&
           (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r'))
        ++cursor;
    if (cursor >= end || *cursor != '{')
        return nullptr;

    // Brace-balance walk. Strings are honoured so we don't count `{` / `}`
    // inside JSON string values. Escapes inside strings are skipped.
    const char *valueStart = cursor;
    int depth = 0;
    bool inString = false;
    while (cursor < end) {
        const char c = *cursor;
        if (inString) {
            if (c == '\\') {
                ++cursor; // skip the escaped byte verbatim
                if (cursor < end)
                    ++cursor;
                continue;
            }
            if (c == '"')
                inString = false;
            ++cursor;
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0) {
                ++cursor;
                *outLen = static_cast<size_t>(cursor - valueStart);
                return valueStart;
            }
        }
        ++cursor;
    }
    return nullptr;
}

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
// mid-write — config never committed. A priority boost alone doesn't prevent this
// because vTaskDelay(0) is an explicit yield regardless of priority.
// Fix: hold g_lvglMutex for the entire write so lv_task_handler() is blocked.
// LVGL ticks that fire during the write see "mutex timeout" and skip — harmless
// since the device reboots immediately after. On failure the mutex is released
// so the UI recovers. BurnOverlay is shown while holding the mutex so the LCD
// gives feedback on both success (reboot wipes it) and failure (error state).
void handlePutConfig(const char *jsonLine) {
#ifdef ARDUINO
    LOG_INFO("USB", "heap.largest_free=%u before PUT_CONFIG",
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
#endif

    const size_t lineLen = strlen(jsonLine);
    size_t written = 0;
    const char *payloadStart = findPayloadSlice(jsonLine, lineLen, &written);
    if (!payloadStart || written == 0) {
        LOG_WARN("USB", "PUT_CONFIG: missing or malformed payload field");
        UsbComm::sendLine("{\"status\":\"error\",\"message\":\"missing_payload\"}");
        return;
    }

    // Take the LVGL mutex before the write. This blocks lv_task_handler() for
    // the entire duration so the OOM assert cannot fire mid-write. Priority
    // inheritance raises our effective priority to TASK_PRIO_UI while we wait,
    // then we boost further to TASK_PRIO_UI+1 so flash ops are uninterrupted.
    // On the success path we call esp_restart() and never release; on failure
    // we release so the UI recovers.
    const bool mutexTaken = (xSemaphoreTake(g_lvglMutex, pdMS_TO_TICKS(100)) == pdTRUE);
    if (!mutexTaken) {
        LOG_WARN("USB", "PUT_CONFIG: could not acquire LVGL mutex — proceeding");
    }
    vTaskPrioritySet(nullptr, TASK_PRIO_UI + 1);
    // BurnOverlay::show() calls lv_refr_now(), which runs
    // DisplayDriver::flushCallback inline on this (USB) task — see the
    // task-coupling note in src/ui/burn_overlay.cpp::show(). The LVGL mutex
    // taken above is what makes that safe: it serialises LVGL state and SPI
    // access with the UI task. Do not call show() without holding the mutex,
    // and do not relax the mutex contract here without revisiting show().
    BurnOverlay::show();

    bool ok = StorageDriver::writeFileAtomic(
        CONFIG_PATH_DASHBOARD, reinterpret_cast<const uint8_t *>(payloadStart), written);
    if (!ok) {
        BurnOverlay::showError(BurnOverlay::ErrorReason::WriteFailed);
        vTaskPrioritySet(nullptr, TASK_PRIO_USB);
        if (mutexTaken)
            xSemaphoreGive(g_lvglMutex);
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
// CMD_PUT_FILE — chunked, base64-encoded file write to storage
// ---------------------------------------------------------------------------

constexpr uint32_t CHUNK_TIMEOUT_MS = 10000;

struct ChunkTransfer {
    char path[CFG_MAX_PATH_LEN];
    uint32_t expectedIdx;
    uint32_t total;
    uint32_t lastActivityMs;
};
static ChunkTransfer s_chunk = {{0}, 0, 0, 0};

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
static const char *kAllowedPutPrefixes[] = {
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
    const int rc =
        mbedtls_base64_decode(reinterpret_cast<unsigned char *>(s_rxBuf), sizeof(s_rxBuf), &decoded,
                              reinterpret_cast<const unsigned char *>(b64), strlen(b64));
    if (rc != 0) {
        UsbComm::sendLine("{\"status\":\"error\",\"message\":\"b64_decode\"}");
        abortChunkTransfer("b64_decode");
        return;
    }

    if (!StorageDriver::appendChunk(reinterpret_cast<const uint8_t *>(s_rxBuf), decoded)) {
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

// Last time the host sent a command. Used by UsbComm::isHostActive() for the
// top bar "USB connected" icon. Updated on every received command (volatile is
// fine — single writer, single reader, atomic on 32-bit ESP32).
static volatile uint32_t s_lastHostCmdMs = 0;

// Deferred-action flags live in `runtime/pending_actions.h` — shared with
// ble_server.cpp so the UI task drains both transports in one place (#893).

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
            Serial.println("{\"status\":\"error\",\"message\":\"parse_error\"}");
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
        case UsbComm::CMD_GET_CONFIG: {
            // Stream the on-disk dashboard.json straight to the host in fixed
            // 256-byte chunks. Avoids the ~20 KB contiguous malloc the previous
            // readFile() variant needed (issue #576). Newlines in the file
            // would break the line-delimited Serial protocol, so they're
            // mapped to spaces inside streamFileTo (JSON treats \n / \r as
            // whitespace anyway).
            if (!StorageDriver::fileExists(CONFIG_PATH_DASHBOARD)) {
                UsbComm::sendLine("{\"status\":\"error\",\"message\":\"config_not_found\"}");
                break;
            }
            // Bracket the 3-call burst with the logger mutex so a logger emit
            // from another task can't slip in between the prefix / body / tail.
            const bool locked = Logger::lockUart(pdMS_TO_TICKS(50));
            Serial.print("{\"status\":\"ok\",\"config\":");
            const size_t streamed = StorageDriver::streamFileTo(
                CONFIG_PATH_DASHBOARD, Serial, true /* replaceNewlinesWithSpaces */);
            Serial.println("}");
            if (locked)
                Logger::unlockUart();
            if (streamed == 0) {
                LOG_WARN("USB", "GET_CONFIG: stream produced 0 bytes for %s",
                         CONFIG_PATH_DASHBOARD);
            } else {
                LOG_INFO("USB", "GET_CONFIG: sent %u bytes", static_cast<unsigned>(streamed));
            }
            break;
        }
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
            LOG_VDEBUG("USB", "Unhandled cmd: 0x%02X", cmd);
            UsbComm::sendLine("{\"status\":\"ok\"}");
            break;
    }
}

// Drain queued CAN scan frames and write them to Serial.
// Each frame is serialized as: {"can":1,"id":<id>,"len":<n>,"d":[b0,...,bn]}\n
// Called from tick() on the USB task — single writer, safe to call Serial.print().
void drainCanScanQueue() {
    if (!s_canScanQueue)
        return;

    UsbComm::CanScanFrame frame;
    // Drain at most 32 frames per tick to avoid blocking telemetry
    uint8_t drained = 0;
    while (drained < 32 && xQueueReceive(s_canScanQueue, &frame, 0) == pdTRUE) {
        // Max line length: {"can":1,"id":536870911,"len":8,"d":[255,255,255,255,255,255,255,255]}
        // ≈ 70 chars — 96-byte buffer is safe.
        char buf[96];
        char *p = buf;
        const char *limit = buf + sizeof(buf) - 4; // reserve \n\0

        p += snprintf(p, static_cast<size_t>(limit - p), "{\"can\":1,\"id\":%lu,\"len\":%u,\"d\":[",
                      static_cast<unsigned long>(frame.id), static_cast<unsigned>(frame.len));

        for (uint8_t i = 0; i < frame.len && p < limit; i++) {
            if (i > 0 && p < limit)
                *p++ = ',';
            p += snprintf(p, static_cast<size_t>(limit - p), "%u",
                          static_cast<unsigned>(frame.data[i]));
        }

        if (p < limit)
            *p++ = ']';
        if (p < limit)
            *p++ = '}';
        if (p < limit)
            *p++ = '\n';
        *p = '\0';

        UsbComm::sendLine(buf);
        drained++;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void UsbComm::init() {
    s_rxPos = 0;
    s_tickCount = 0;
    memset(s_rxBuf, 0, sizeof(s_rxBuf));
    // s_canScanQueue is allocated lazily on first CMD_CAN_SCAN_START so we
    // don't carry ~1 KB of DRAM for a feature that's rarely used (#976).
    LOG_INFO("USB", "USB comm initialized");
}

void UsbComm::sendLine(const char *line) {
    if (!line)
        return;
    const size_t len = strlen(line);
    const bool needsNewline = len == 0 || line[len - 1] != '\n';

    // The mutex serializes wire-protocol writers (this function) against
    // logger emit() calls from any other task. Lock failure falls through
    // unprotected — better to risk one fragmented line than to silently drop
    // a command ack the studio is waiting on.
    const bool locked = Logger::lockUart(pdMS_TO_TICKS(50));
    Serial.write(reinterpret_cast<const uint8_t *>(line), len);
    if (needsNewline)
        Serial.write('\n');
    if (locked)
        Logger::unlockUart();
}

bool UsbComm::isHostActive() {
    constexpr uint32_t HOST_TIMEOUT_MS = 5000;
    if (s_lastHostCmdMs == 0)
        return false;
    return (millis() - s_lastHostCmdMs) < HOST_TIMEOUT_MS;
}

void UsbComm::updateCanStats(uint32_t fpsX10, uint32_t errors) {
    s_canStats.fpsX10 = fpsX10;
    s_canStats.errors = errors;
    s_canStatsPending = true;
}

bool UsbComm::pushCanFrame(const CanScanFrame &frame) {
    if (!s_canScanMode)
        return false;
    // Capture the queue handle under the spinlock so a concurrent
    // CMD_CAN_SCAN_STOP on the USB task cannot vQueueDelete it between our
    // read and our xQueueSend (#1009). The MUX section is tiny — one pointer
    // load with interrupts disabled on the current core.
    QueueHandle_t q;
    portENTER_CRITICAL(&s_canScanQueueMux);
    q = s_canScanQueue;
    portEXIT_CRITICAL(&s_canScanQueueMux);
    if (!q)
        return false;
    // Non-blocking: drop frame and count it if queue is full
    if (xQueueSend(q, &frame, 0) != pdTRUE) {
        s_scanDrops++;
        return false;
    }
    return true;
}

void UsbComm::tick() {
    // Abort any chunked transfer that has stalled (host crashed mid-stream
    // or unplugged) — leaves storage in a clean state.
    if (StorageDriver::isChunkedWriteOpen() &&
        (millis() - s_chunk.lastActivityMs) > CHUNK_TIMEOUT_MS) {
        abortChunkTransfer("idle timeout");
    }

    while (Serial.available() > 0) {
        char c = static_cast<char>(Serial.read());
        // Update host activity on any byte received, not just complete commands.
        // This keeps the top-bar USB icon green while the studio's serial port
        // is open even between command lines.
        s_lastHostCmdMs = millis();

        if (c == '\n') {
            s_rxBuf[s_rxPos] = '\0';
            if (s_rxPos > 0) {
                handleCommand(s_rxBuf);
            }
            s_rxPos = 0;
        } else if (s_rxPos < USB_RX_BUF_SIZE - 1) {
            s_rxBuf[s_rxPos++] = c;
        } else {
            LOG_WARN("USB", "RX buffer overflow — discarding packet");
            s_rxPos = 0;
        }
    }

    // Drain CAN scan queue — send queued frames before telemetry
    drainCanScanQueue();

    // Emit CAN health stats if the CAN task pushed new data
    if (s_canStatsPending) {
        s_canStatsPending = false;
        // Format: {"can_stat":1,"fps":12.5,"errors":0}\n
        char statBuf[72];
        const CanHealthStats stats = {s_canStats.fpsX10, s_canStats.errors};
        const int n =
            snprintf(statBuf, sizeof(statBuf), "{\"can_stat\":1,\"fps\":%lu.%lu,\"errors\":%lu}\n",
                     static_cast<unsigned long>(stats.fpsX10 / 10),
                     static_cast<unsigned long>(stats.fpsX10 % 10),
                     static_cast<unsigned long>(stats.errors));
        if (n > 0 && static_cast<size_t>(n) < sizeof(statBuf)) {
            UsbComm::sendLine(statBuf);
        } else {
            // 72-byte cap fits the worst-case max-uint values today, but if a
            // future protocol bump grows the payload we want to know rather
            // than ship malformed JSON to the host (#936).
            LOG_WARN("USB", "can_stat payload truncated (n=%d, cap=%u)", n,
                     static_cast<unsigned>(sizeof(statBuf)));
        }
    }

    if (++s_tickCount >= TELE_PERIOD_TICKS) {
        s_tickCount = 0;
        sendTelemetry();
    }
}
