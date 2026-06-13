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

extern SemaphoreHandle_t g_lvglMutex;

namespace {

constexpr uint8_t CAN_SCAN_QUEUE_DEPTH = 64;
QueueHandle_t s_canScanQueue = nullptr;
portMUX_TYPE s_canScanQueueMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool s_canScanMode = false;

uint32_t s_scanDrops = 0;

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

    for (size_t i = 0; i < len; ++i) {
        const unsigned char c = static_cast<unsigned char>(path[i]);
        if (c < 0x20 || c == 0x7F)
            return false;
    }
    if (strstr(path, "//"))
        return false;
    if (strstr(path, ".."))
        return false;
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

    UsbCommInternal::invokeBurnOverlayShow();
    vTaskDelay(pdMS_TO_TICKS(BURN_OVERLAY_RENDER_GRACE_MS));

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

        UsbCommInternal::invokeBurnOverlayShowError(0);
        LOG_ERROR("USB", "PUT_CONFIG: storage write failed");
        UsbComm::sendLine("{\"status\":\"error\",\"message\":\"write_failed\"}");
        return;
    }

    LOG_INFO("USB", "PUT_CONFIG: dashboard.json updated (%u bytes) — rebooting", written);
    UsbComm::sendLine("{\"status\":\"ok\",\"rebooting\":true}");
    Serial.flush();
    esp_restart();
}

void handleScreenSettings(const JsonObjectConst &obj) {
    JsonVariantConst brightnessVar = obj["brightness"];
    if (!brightnessVar.isNull()) {
        const uint8_t brightness = brightnessVar.as<uint8_t>();
        if (xSemaphoreTake(g_lvglMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            LVGL_HOLD_GUARD(::PerfCounters::MUTEX_HOLD_USB);
            SettingsPage::applyFromUsb(brightness);
            xSemaphoreGive(g_lvglMutex);
        } else {
            LOG_WARN("USB", "Screen settings: could not acquire LVGL mutex");
            UsbComm::sendLine("{\"status\":\"error\",\"message\":\"busy\"}");
            return;
        }
    }

    JsonVariantConst rotationVar = obj["rotation"];
    if (!rotationVar.isNull()) {
        const uint16_t rotation = rotationVar.as<uint16_t>();
        if ((rotation == 0 || rotation == 180) && rotation != RotationConfig::getOffsetDeg()) {
            LOG_INFO("USB", "Rotation change requested: %u° — rebooting", rotation);
            UsbComm::sendLine("{\"status\":\"ok\",\"rebooting\":true}");
            Serial.flush();
            RotationConfig::applyAndReboot(rotation);
        }
    }

    UsbComm::sendLine("{\"status\":\"ok\"}");
}

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

    BufferPrint sink(buf + kPrefixLen, needed - kPrefixLen - 2);
    const size_t streamed = StorageDriver::streamFileTo(CONFIG_PATH_DASHBOARD, sink, true);

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

    const size_t jsonLen = strlen(jsonLine);

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

            sendTypedConfigGet(CONFIG_PATH_DEVICE, "device_config", nullptr);
            break;
        case UsbComm::CMD_PUT_DEVICE_CONFIG:
            handlePutDeviceConfig(doc.as<JsonObjectConst>());
            break;
        case UsbComm::CMD_GET_INPUT_BINDINGS:

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

            QueueHandle_t fresh = nullptr;
            if (!s_canScanQueue) {
                fresh = xQueueCreate(CAN_SCAN_QUEUE_DEPTH, sizeof(UsbComm::CanScanFrame));
                if (!fresh) {
                    LOG_ERROR("USB", "CAN scan start: queue alloc failed");
#if APP_USB_CAN_SCAN_FAIL_LOUD

                    abort();
#endif

                    ErrorStore::push(ERROR_SRC_SYSTEM, "SCAN_QUEUE", "CAN scan queue alloc failed");
                    UsbComm::sendLine("{\"status\":\"error\",\"error\":\"queue_alloc_failed\"}");
                    break;
                }
            }

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

            QueueHandle_t toDelete;
            portENTER_CRITICAL(&s_canScanQueueMux);
            toDelete = s_canScanQueue;
            s_canScanQueue = nullptr;
            portEXIT_CRITICAL(&s_canScanQueueMux);

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

            LOG_WARN("USB", "Unknown cmd: 0x%02X — replying unknown_command", cmd);
            UsbComm::sendLine("{\"status\":\"error\",\"message\":\"unknown_command\"}");
            break;
    }
}

} // namespace UsbCommInternal
