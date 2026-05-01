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
#include "runtime/signal_store.h"
#include "can/signal_map.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_system.h> // esp_restart()

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

// Handle CMD_PUT_CONFIG (0x02): write new dashboard.json to SPIFFS, then reboot.
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

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, jsonLine);
    if (err) {
        LOG_WARN("USB", "JSON parse error: %s", err.c_str());
        Serial.println("{\"status\":\"error\",\"message\":\"parse_error\"}");
        return;
    }

    switch (cmd) {
        case UsbComm::CMD_GET_STATUS: {
            char resp[80];
            snprintf(resp, sizeof(resp),
                     "{\"status\":\"ok\",\"version\":\"%s\",\"protocol\":%u}", APP_VERSION_STR,
                     static_cast<unsigned>(USB_PROTOCOL_VERSION));
            Serial.println(resp);
            break;
        }
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

    if (++s_tickCount >= TELE_PERIOD_TICKS) {
        s_tickCount = 0;
        sendTelemetry();
    }
}
