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
#include "hal/storage/storage_driver.h"
#include "config/config_loader.h"
#include "ui/settings_page.h"
#include "ui/theme_manager.h"
#include "runtime/signal_store.h"
#include "can/signal_map.h"

#include <atomic>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_system.h> // esp_restart()
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
static constexpr uint8_t CAN_SCAN_QUEUE_DEPTH = 64;
static QueueHandle_t s_canScanQueue = nullptr;
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

        int n = snprintf(p, static_cast<size_t>(end - p), "\"%s\":%.3g", TELE_SIGNALS[i].name,
                         static_cast<double>(val));
        if (n <= 0 || p + n >= end)
            break;
        p += n;
    }

    if (p + 3 <= end) {
        *p++ = '}';
        *p++ = '}';
        *p++ = '\n';
        *p = '\0';
        Serial.print(buf);
    }
}

// ---------------------------------------------------------------------------
// Command handlers
// ---------------------------------------------------------------------------

// Handle CMD_PUT_CONFIG (0x02): write new dashboard.json to SD, then reboot.
// After ArduinoJson parses into doc, s_rxBuf is no longer needed for reading,
// so we reuse it as the serialization buffer for the payload.
void handlePutConfig(const char *jsonLine) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, jsonLine);
    if (err) {
        LOG_WARN("USB", "PUT_CONFIG parse error: %s", err.c_str());
        Serial.println("{\"status\":\"error\",\"message\":\"parse_error\"}");
        return;
    }

    JsonObjectConst payload = doc["payload"].as<JsonObjectConst>();
    if (payload.isNull()) {
        LOG_WARN("USB", "PUT_CONFIG: missing payload field");
        Serial.println("{\"status\":\"error\",\"message\":\"missing_payload\"}");
        return;
    }

    // Reuse s_rxBuf as the serialization buffer — doc is independent of jsonLine.
    size_t written = serializeJson(payload, s_rxBuf, sizeof(s_rxBuf));
    if (written == 0) {
        LOG_ERROR("USB", "PUT_CONFIG: serialization failed");
        Serial.println("{\"status\":\"error\",\"message\":\"serialize_failed\"}");
        return;
    }

    bool ok = StorageDriver::writeFile(CONFIG_PATH_DASHBOARD,
                                       reinterpret_cast<const uint8_t *>(s_rxBuf), written);
    if (!ok) {
        LOG_ERROR("USB", "PUT_CONFIG: SD write failed");
        Serial.println("{\"status\":\"error\",\"message\":\"write_failed\"}");
        return;
    }

    LOG_INFO("USB", "PUT_CONFIG: dashboard.json updated (%u bytes) — rebooting", written);
    Serial.flush();
    Serial.println("{\"status\":\"ok\"}");
    Serial.flush();
    delay(150);
    esp_restart();
}

// ---------------------------------------------------------------------------
// CMD_PUT_FILE — chunked, base64-encoded file write to SD
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
        StorageDriver::endChunkedWrite();
    }
    s_chunk.path[0] = '\0';
    s_chunk.expectedIdx = 0;
    s_chunk.total = 0;
}

bool isPathSafe(const char *path) {
    if (!path || path[0] != '/')
        return false;
    const size_t len = strlen(path);
    if (len == 1 || len >= CFG_MAX_PATH_LEN)
        return false;
    if (strstr(path, ".."))
        return false;
    return true;
}

void handlePutFile(const JsonObjectConst &obj) {
    const char *path = obj["path"];
    const uint32_t total = obj["total"] | 0u;
    const uint32_t idx = obj["idx"] | 0u;
    const char *b64 = obj["data"];

    if (!isPathSafe(path) || !b64 || total == 0 || idx >= total) {
        Serial.println("{\"status\":\"error\",\"message\":\"bad_args\"}");
        abortChunkTransfer("bad_args");
        return;
    }

    if (idx == 0) {
        abortChunkTransfer("new transfer");
        if (!StorageDriver::beginChunkedWrite(path)) {
            Serial.println("{\"status\":\"error\",\"message\":\"open_failed\"}");
            return;
        }
        strlcpy(s_chunk.path, path, sizeof(s_chunk.path));
        s_chunk.total = total;
        s_chunk.expectedIdx = 0;
    } else if (!StorageDriver::isChunkedWriteOpen() || s_chunk.expectedIdx != idx ||
               strcmp(s_chunk.path, path) != 0) {
        Serial.println("{\"status\":\"error\",\"message\":\"out_of_sequence\"}");
        abortChunkTransfer("out_of_sequence");
        return;
    }

    // Decode base64 in-place into s_rxBuf — ArduinoJson 7 has already copied
    // path/data into the JsonDocument's pool, so the source line is free.
    size_t decoded = 0;
    const int rc = mbedtls_base64_decode(reinterpret_cast<unsigned char *>(s_rxBuf),
                                         sizeof(s_rxBuf), &decoded,
                                         reinterpret_cast<const unsigned char *>(b64),
                                         strlen(b64));
    if (rc != 0) {
        Serial.println("{\"status\":\"error\",\"message\":\"b64_decode\"}");
        abortChunkTransfer("b64_decode");
        return;
    }

    if (!StorageDriver::appendChunk(reinterpret_cast<const uint8_t *>(s_rxBuf), decoded)) {
        Serial.println("{\"status\":\"error\",\"message\":\"write_failed\"}");
        abortChunkTransfer("write_failed");
        return;
    }

    s_chunk.expectedIdx = idx + 1;
    s_chunk.lastActivityMs = millis();

    if (idx == total - 1) {
        StorageDriver::endChunkedWrite();
        LOG_INFO("USB", "PUT_FILE done: %s (%u chunks)", s_chunk.path, total);
        s_chunk.path[0] = '\0';
        s_chunk.expectedIdx = 0;
        s_chunk.total = 0;
    }

    Serial.println("{\"status\":\"ok\"}");
}

void handleScreenSettings(const JsonObjectConst &obj) {
    uint8_t brightness = obj["brightness"] | 80;
    uint32_t sleepS = obj["sleep"] | 0u;

    if (xSemaphoreTake(g_lvglMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        SettingsPage::applyFromUsb(brightness, sleepS);
        xSemaphoreGive(g_lvglMutex);
        Serial.println("{\"status\":\"ok\"}");
    } else {
        LOG_WARN("USB", "Screen settings: could not acquire LVGL mutex");
        Serial.println("{\"status\":\"error\",\"message\":\"busy\"}");
    }
}

// Last time the host sent a command. Used by UsbComm::isHostActive() for the
// top bar "USB connected" icon. Updated on every received command (volatile is
// fine — single writer, single reader, atomic on 32-bit ESP32).
static volatile uint32_t s_lastHostCmdMs = 0;

// Deferred-action flags — set by command handlers, consumed by the UI task in
// main.cpp. Mirrors the pattern used by ble_server.cpp for the same actions.
static std::atomic<bool> s_pendingDayNightToggle{false};
static std::atomic<bool> s_pendingCalibration{false};

void handleCommand(const char *jsonLine) {
    s_lastHostCmdMs = millis();
    LOG_DEBUG("USB", "Received command: %.40s...", jsonLine);

    // Peek at cmd using a filter — avoids loading the full PUT_CONFIG payload
    JsonDocument cmdFilter;
    cmdFilter["cmd"] = true;
    JsonDocument peekDoc;
    deserializeJson(peekDoc, jsonLine, DeserializationOption::Filter(cmdFilter));
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
        DeserializationError err = deserializeJson(doc, jsonLine);
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
    DeserializationError err = deserializeJson(doc, jsonLine);
    if (err) {
        LOG_WARN("USB", "JSON parse error: %s", err.c_str());
        Serial.println("{\"status\":\"error\",\"message\":\"parse_error\"}");
        return;
    }

    switch (cmd) {
        case UsbComm::CMD_GET_STATUS: {
            char resp[112];
            snprintf(
                resp, sizeof(resp),
                "{\"status\":\"ok\",\"version\":\"%s\",\"protocol\":%u,\"is_day\":%d}",
                APP_VERSION_STR, static_cast<unsigned>(USB_PROTOCOL_VERSION),
                ThemeManager::isDayMode() ? 1 : 0);
            Serial.println(resp);
            break;
        }
        case UsbComm::CMD_SCREEN_SETTINGS:
            handleScreenSettings(doc.as<JsonObjectConst>());
            break;
        case UsbComm::CMD_TOGGLE_DAY_NIGHT:
            s_pendingDayNightToggle.store(true, std::memory_order_relaxed);
            LOG_INFO("USB", "CMD: day/night toggle queued");
            Serial.println("{\"status\":\"ok\"}");
            break;
        case UsbComm::CMD_CALIBRATE_TOUCH:
            s_pendingCalibration.store(true, std::memory_order_relaxed);
            LOG_INFO("USB", "CMD: calibration queued");
            Serial.println("{\"status\":\"ok\"}");
            break;
        case UsbComm::CMD_CAN_SCAN_START:
            s_scanDrops = 0;
            s_canScanMode = true;
            if (s_canScanQueue) xQueueReset(s_canScanQueue);
            LOG_INFO("USB", "CAN scan started");
            Serial.println("{\"status\":\"ok\"}");
            break;
        case UsbComm::CMD_CAN_SCAN_STOP: {
            s_canScanMode = false;
            if (s_canScanQueue) xQueueReset(s_canScanQueue);
            LOG_INFO("USB", "CAN scan stopped — drops: %lu", (unsigned long)s_scanDrops);
            char stopResp[64];
            snprintf(stopResp, sizeof(stopResp), "{\"status\":\"ok\",\"drops\":%lu}",
                     (unsigned long)s_scanDrops);
            Serial.println(stopResp);
            break;
        }
        default:
            LOG_DEBUG("USB", "Unhandled cmd: 0x%02X", cmd);
            Serial.println("{\"status\":\"ok\"}");
            break;
    }
}

// Drain queued CAN scan frames and write them to Serial.
// Each frame is serialized as: {"can":1,"id":<id>,"len":<n>,"d":[b0,...,bn]}\n
// Called from tick() on the USB task — single writer, safe to call Serial.print().
void drainCanScanQueue() {
    if (!s_canScanQueue) return;

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
                      static_cast<unsigned long>(frame.id),
                      static_cast<unsigned>(frame.len));

        for (uint8_t i = 0; i < frame.len && p < limit; i++) {
            if (i > 0 && p < limit) *p++ = ',';
            p += snprintf(p, static_cast<size_t>(limit - p), "%u",
                          static_cast<unsigned>(frame.data[i]));
        }

        if (p < limit) *p++ = ']';
        if (p < limit) *p++ = '}';
        if (p < limit) *p++ = '\n';
        *p = '\0';

        Serial.print(buf);
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
    s_canScanQueue = xQueueCreate(CAN_SCAN_QUEUE_DEPTH, sizeof(CanScanFrame));
    if (!s_canScanQueue) {
        LOG_ERROR("USB", "Failed to create CAN scan queue");
    }
    LOG_INFO("USB", "USB comm initialized");
}

bool UsbComm::takePendingDayNightToggle() {
    return s_pendingDayNightToggle.exchange(false, std::memory_order_relaxed);
}

bool UsbComm::takePendingCalibration() {
    return s_pendingCalibration.exchange(false, std::memory_order_relaxed);
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
    if (!s_canScanMode || !s_canScanQueue) return false;
    // Non-blocking: drop frame and count it if queue is full
    if (xQueueSend(s_canScanQueue, &frame, 0) != pdTRUE) {
        s_scanDrops++;
        return false;
    }
    return true;
}

void UsbComm::tick() {
    // Abort any chunked transfer that has stalled (host crashed mid-stream
    // or unplugged) — leaves the SD in a clean state.
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
        snprintf(statBuf, sizeof(statBuf), "{\"can_stat\":1,\"fps\":%lu.%lu,\"errors\":%lu}\n",
                 static_cast<unsigned long>(stats.fpsX10 / 10),
                 static_cast<unsigned long>(stats.fpsX10 % 10),
                 static_cast<unsigned long>(stats.errors));
        Serial.print(statBuf);
    }

    if (++s_tickCount >= TELE_PERIOD_TICKS) {
        s_tickCount = 0;
        sendTelemetry();
    }
}
