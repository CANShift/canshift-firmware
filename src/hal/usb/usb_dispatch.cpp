#include "usb_comm_internal.h"

#include "app_config.h"
#include "board_config.h"
#include "config/json_reader.h"
#include "config/rotation_config.h"
#include "diag/logger.h"
#include "diag/lvgl_lock_guard.h"
#include "runtime/pending_actions.h"
#include "ui/settings_page.h"
#include "ui/theme_manager.h"

#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#ifdef ARDUINO
    #include <Arduino.h>
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern SemaphoreHandle_t g_lvglMutex;

namespace {

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

} // namespace

namespace UsbCommInternal {

struct CmdHeapGuard {
    uint8_t cmd;
    uint32_t largestBefore;
    explicit CmdHeapGuard(uint8_t c)
        : cmd(c), largestBefore(static_cast<uint32_t>(
                      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT))) {}
    ~CmdHeapGuard() {
        const uint32_t after = static_cast<uint32_t>(
            heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        if (largestBefore > after && (largestBefore - after) > 1024) {
            LOG_DEBUG("HEAP", "cmd=0x%02x largest %lu -> %lu (-%lu)", static_cast<unsigned>(cmd),
                      static_cast<unsigned long>(largestBefore), static_cast<unsigned long>(after),
                      static_cast<unsigned long>(largestBefore - after));
        }
    }
};

uint8_t extractCmdByte(const char *jsonLine) {
    const char *cmdKey = strstr(jsonLine, "\"cmd\":");
    if (cmdKey == nullptr)
        return 0;
    char *endPtr = nullptr;
    const long parsed = strtol(cmdKey + 6, &endPtr, 10);
    if (endPtr == cmdKey + 6 || parsed < 0 || parsed > 0xFF)
        return 0;
    return static_cast<uint8_t>(parsed);
}

void handleCommand(const char *jsonLine) {
    s_lastHostCmdMs = millis();
    LOG_VDEBUG("USB", "Received command: %.40s...", jsonLine);

    const size_t jsonLen = strlen(jsonLine);
    const uint8_t cmd = extractCmdByte(jsonLine);
    CmdHeapGuard heapGuard{cmd};

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

    if (cmd == UsbComm::CMD_OTA_WRITE) {
        handleOtaWriteRaw(jsonLine);
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
        case UsbComm::CMD_OTA_BEGIN:
            handleOtaBegin(doc.as<JsonObjectConst>());
            break;
        case UsbComm::CMD_OTA_END:
            handleOtaEnd(doc.as<JsonObjectConst>());
            break;
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
        case UsbComm::CMD_REBOOT:
            LOG_INFO("USB", "CMD_REBOOT — restarting");
            UsbComm::sendLine("{\"status\":\"ok\",\"restart\":true}");
            delay(50);
            esp_restart();
            break;
        case UsbComm::CMD_CAN_SCAN_START:
            handleCanScanStart();
            break;
        case UsbComm::CMD_CAN_SCAN_STOP:
            handleCanScanStop();
            break;
        default:

            LOG_WARN("USB", "Unknown cmd: 0x%02X — replying unknown_command", cmd);
            UsbComm::sendLine("{\"status\":\"error\",\"message\":\"unknown_command\"}");
            break;
    }
}

} // namespace UsbCommInternal
