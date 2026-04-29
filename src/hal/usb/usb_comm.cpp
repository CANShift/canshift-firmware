// usb_comm.cpp — USB serial communication layer
//
// Phase 1 implementation: JSON line-based command receiver + telemetry push.
// Desktop sends: {"cmd": <id>, ...fields}\n
// Firmware responds with a JSON line.
// Firmware also pushes telemetry proactively every USB_TELE_PERIOD_TICKS ticks.
//
// Telemetry format: {"tele":1,"v":{"rpm":1234.5,"coolant_temp_c":89.2,...}}\n
// Command response: {"status":"ok"} or {"status":"error","message":"..."}\n

#include "usb_comm.h"
#include "board_config.h"
#include "app_config.h"
#include "diag/logger.h"
#include "hal/storage/storage_driver.h"
#include "config/config_loader.h"
#include "ui/settings_page.h"
#include "runtime/signal_store.h"
#include "can/signal_map.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_system.h> // esp_restart()

// Forward-declared FreeRTOS LVGL mutex from main.cpp
extern SemaphoreHandle_t g_lvglMutex;

// ---------------------------------------------------------------------------
// Telemetry push — sent every USB_TELE_PERIOD_TICKS × 20ms ≈ 200ms
// ---------------------------------------------------------------------------

// How many tick() calls between telemetry pushes. tick() runs every 20ms.
static constexpr uint8_t USB_TELE_PERIOD_TICKS = 10; // 200ms

// Static TX buffer — sized for ~20 signals × ~24 chars + wrapper overhead
static constexpr size_t TELE_BUF_SIZE = 768;
static char s_teleBuf[TELE_BUF_SIZE];

namespace {

// Signal name table — indexed by SignalId, empty string = not reported
struct TeleEntry {
    SignalId id;
    const char *name;
};

// All signals exposed in signals.json / signal_map.h
static constexpr TeleEntry TELE_SIGNALS[] = {
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

static constexpr size_t TELE_SIGNAL_COUNT =
    sizeof(TELE_SIGNALS) / sizeof(TELE_SIGNALS[0]);

// Send one telemetry line. Skips invalid (timed-out) signals.
void sendTelemetry() {
    // Build the JSON manually into s_teleBuf to avoid heap allocation.
    // Format: {"tele":1,"v":{"rpm":1234.1,"coolant_temp_c":89.0,...}}
    // Only valid signals are included to reduce noise.

    char *p = s_teleBuf;
    const char *end = s_teleBuf + TELE_BUF_SIZE - 2; // reserve \n\0

    // Open object
    const char *prefix = "{\"tele\":1,\"v\":{";
    size_t prefixLen = strlen(prefix);
    if (p + prefixLen >= end) return;
    memcpy(p, prefix, prefixLen);
    p += prefixLen;

    bool first = true;
    for (size_t i = 0; i < TELE_SIGNAL_COUNT; i++) {
        if (!SignalStore::isValid(TELE_SIGNALS[i].id)) continue;
        float val = SignalStore::read(TELE_SIGNALS[i].id);

        // comma separator
        if (!first) {
            if (p >= end) break;
            *p++ = ',';
        }
        first = false;

        // "name":value
        int n = snprintf(p, static_cast<size_t>(end - p), "\"%s\":%.3g",
                         TELE_SIGNALS[i].name, static_cast<double>(val));
        if (n <= 0 || p + n >= end) break;
        p += n;
    }

    // Close + newline
    if (p + 3 <= end) {
        *p++ = '}';
        *p++ = '}';
        *p++ = '\n';
        *p = '\0';
        Serial.print(s_teleBuf);
    }
}

// ---------------------------------------------------------------------------
// Receive state machine
// ---------------------------------------------------------------------------

static char s_rxBuf[USB_RX_BUF_SIZE];
static size_t s_rxPos = 0;

// Serialization buffer for writing dashboard.json back to SPIFFS.
static char s_writeBuf[CONFIG_JSON_DOC_DASHBOARD];

// Tick counter for telemetry scheduling
static uint8_t s_tickCount = 0;

// Handle CMD_PUT_CONFIG (0x02): receive dashboard JSON from studio,
// write to SPIFFS, then reboot so the new config takes effect cleanly.
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

    size_t written = serializeJson(payload, s_writeBuf, sizeof(s_writeBuf));
    if (written == 0) {
        LOG_ERROR("USB", "PUT_CONFIG: serialization failed (buffer too small?)");
        Serial.println("{\"status\":\"error\",\"message\":\"serialize_failed\"}");
        return;
    }

    bool ok = StorageDriver::writeFile(CONFIG_PATH_DASHBOARD,
                                       reinterpret_cast<const uint8_t *>(s_writeBuf), written);
    if (!ok) {
        LOG_ERROR("USB", "PUT_CONFIG: SPIFFS write failed");
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

void handleScreenSettings(const JsonObjectConst &obj) {
    uint8_t brightness = obj["brightness"] | 80;
    uint8_t contrast = obj["contrast"] | 50;
    uint32_t sleepS = obj["sleep"] | 0u;
    uint16_t rotation = obj["rotation"] | 0u;

    if (xSemaphoreTake(g_lvglMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        SettingsPage::applyFromUsb(brightness, contrast, sleepS, rotation);
        xSemaphoreGive(g_lvglMutex);
        Serial.println("{\"status\":\"ok\"}");
    } else {
        LOG_WARN("USB", "Screen settings: could not acquire LVGL mutex");
        Serial.println("{\"status\":\"error\",\"message\":\"busy\"}");
    }
}

void handleCommand(const char *jsonLine) {
    LOG_DEBUG("USB", "Received command: %.40s...", jsonLine);

    // Peek at cmd first — avoid parsing large PUT_CONFIG payload in full doc
    JsonDocument cmdFilter;
    cmdFilter["cmd"] = true;
    JsonDocument peekDoc;
    deserializeJson(peekDoc, jsonLine, DeserializationOption::Filter(cmdFilter));
    uint8_t cmd = peekDoc["cmd"] | 0;

    if (cmd == UsbComm::CMD_PUT_CONFIG) {
        handlePutConfig(jsonLine);
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
        case UsbComm::CMD_SCREEN_SETTINGS:
            handleScreenSettings(doc.as<JsonObjectConst>());
            break;
        default:
            LOG_DEBUG("USB", "Unhandled cmd: 0x%02X", cmd);
            Serial.println("{\"status\":\"ok\"}");
            break;
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
    LOG_INFO("USB", "USB comm initialized");
}

void UsbComm::tick() {
    // Read available bytes from Serial (UART0 / USB bridge)
    while (Serial.available() > 0) {
        char c = static_cast<char>(Serial.read());

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

    // Push telemetry periodically
    if (++s_tickCount >= USB_TELE_PERIOD_TICKS) {
        s_tickCount = 0;
        sendTelemetry();
    }
}
