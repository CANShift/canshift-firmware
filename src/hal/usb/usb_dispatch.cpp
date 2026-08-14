#include "usb_comm_internal.h"

#include "app_config.h"
#include "board.h"
#include "board_config.h"
#include "config/board_profile_store.h"
#include "config/json_reader.h"
#include "config/rotation_config.h"
#include "diag/logger.h"
#include "diag/lvgl_hold_timer.h"
#include "runtime/lvgl_lock.h"
#include "runtime/device_commands.h"
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

namespace {

void handleSetBoardProfile(const JsonObjectConst &doc) {
    JsonVariantConst payload = doc["payload"];
    if (!payload.is<JsonObjectConst>()) {
        UsbComm::sendError("missing_payload");
        return;
    }
    char blob[1536];
    const size_t n = serializeJson(payload, blob, sizeof blob);
    if (n == 0 || n >= sizeof blob) {
        UsbComm::sendError("blob_too_large");
        return;
    }
    if (!BoardProfileStore::save(blob, n)) {
        UsbComm::sendError("invalid_board_profile");
        return;
    }
    LOG_INFO("USB", "CMD_SET_BOARD_PROFILE — profile stored, restarting");
    UsbComm::sendLine("{\"status\":\"ok\",\"restart\":true}");
    UsbComm::flushAndRestart();
}

void sendStatusResponse() {
    char resp[160];
    const int n = snprintf(resp, sizeof(resp),
                           "{\"status\":\"ok\",\"version\":\"%s\",\"protocol\":%u,\"is_day\":%d,"
                           "\"board_id\":\"%s\"}",
                           APP_VERSION_STR, static_cast<unsigned>(USB_PROTOCOL_VERSION),
                           ThemeManager::isDayMode() ? 1 : 0, kBoard.board_id);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(resp)) {
        LOG_WARN("USB", "GET_STATUS payload truncated (n=%d, cap=%u)", n,
                 static_cast<unsigned>(sizeof(resp)));
        return;
    }
    UsbComm::sendLine(resp);
}

void sendPingResponse() {
    char resp[48];
    const int n = snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"uptime_ms\":%lu}",
                           static_cast<unsigned long>(millis()));
    if (n > 0 && static_cast<size_t>(n) < sizeof(resp)) {
        UsbComm::sendLine(resp);
    }
}

void renderSharedOutcome(DeviceCommands::Outcome outcome) {
    if (outcome == DeviceCommands::Outcome::MissingDayField) {
        UsbComm::sendError("missing_day");
        return;
    }
    if (outcome == DeviceCommands::Outcome::RebootRequested) {
        UsbComm::sendLine("{\"status\":\"ok\",\"restart\":true}");
        UsbComm::flushAndRestart();
        return;
    }
    UsbComm::sendOk();
}

void handleScreenSettings(const JsonObjectConst &obj) {
    JsonVariantConst brightnessVar = obj["brightness"];
    if (!brightnessVar.isNull()) {
        const uint8_t brightness = brightnessVar.as<uint8_t>();
        LvglLock lock(pdMS_TO_TICKS(USB_SCREEN_SETTINGS_MUTEX_TIMEOUT_MS));
        if (!lock.held()) {
            LOG_WARN("USB", "Screen settings: could not acquire LVGL mutex");
            UsbComm::sendError("busy");
            return;
        }
        LVGL_HOLD_TIMER(::PerfCounters::MUTEX_HOLD_USB);
        SettingsPage::applyFromUsb(brightness);
    }

    JsonVariantConst rotationVar = obj["rotation"];
    if (!rotationVar.isNull()) {
        const uint16_t rotation = rotationVar.as<uint16_t>();
        if ((rotation == 0 || rotation == 180) && rotation != RotationConfig::getOffsetDeg()) {
            LOG_INFO("USB", "Rotation change requested: %u° — rebooting", rotation);
            UsbComm::sendOkRebooting();
            Serial.flush();
            RotationConfig::applyAndReboot(rotation);
        }
    }

    UsbComm::sendOk();
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

bool parseOrFail(const char *jsonLine, size_t jsonLen, JsonDocument &doc, const char *abortReason) {
    const DeserializationError err = JsonReader::parse(doc, jsonLine, jsonLen);
    if (!err)
        return true;
    LOG_WARN("USB", "JSON parse error: %s", err.c_str());
    UsbComm::sendError("parse_error");
    if (abortReason != nullptr) {
        abortChunkTransfer(abortReason);
    }
    return false;
}

// PUT_CONFIG and OTA_WRITE read their payload straight off the raw line; PUT_FILE
// parses first but must abort an open chunk transfer when that fails.
bool consumeRawPayloadCommand(uint8_t cmd, const char *jsonLine, size_t jsonLen) {
    if (cmd == UsbComm::CMD_PUT_CONFIG) {
        handlePutConfig(jsonLine);
        return true;
    }
    if (cmd == UsbComm::CMD_OTA_WRITE) {
        handleOtaWriteRaw(jsonLine);
        return true;
    }
    if (cmd != UsbComm::CMD_PUT_FILE) {
        return false;
    }
    JsonDocument doc;
    if (parseOrFail(jsonLine, jsonLen, doc, "parse_error")) {
        handlePutFile(doc.as<JsonObjectConst>());
    }
    return true;
}

struct UsbCommand {
    uint8_t code;
    void (*run)(const JsonObjectConst &);
};

// The xtensa toolchain rejects captureless lambdas in a constexpr initializer,
// so commands that ignore the payload get a named adapter.
void runGetStatus(const JsonObjectConst &) {
    sendStatusResponse();
}
void runPing(const JsonObjectConst &) {
    sendPingResponse();
}
void runGetConfig(const JsonObjectConst &) {
    handleGetConfig();
}
void runGetDeviceConfig(const JsonObjectConst &) {
    sendTypedConfigGet(CONFIG_PATH_DEVICE, "device_config", nullptr);
}
void runGetInputBindings(const JsonObjectConst &) {
    sendTypedConfigGet(CONFIG_PATH_INPUTS, "input_bindings", "input_bindings");
}
void runCanScanStart(const JsonObjectConst &) {
    handleCanScanStart();
}
void runCanScanStop(const JsonObjectConst &) {
    handleCanScanStop();
}
void runObdReadDtc(const JsonObjectConst &) {
    handleObdReadDtc();
}
void runObdClearDtc(const JsonObjectConst &) {
    handleObdClearDtc();
}

constexpr UsbCommand kUsbCommands[] = {
    {UsbComm::CMD_GET_STATUS, &runGetStatus},
    {UsbComm::CMD_PING, &runPing},
    {UsbComm::CMD_GET_CONFIG, &runGetConfig},
    {UsbComm::CMD_GET_DEVICE_CONFIG, &runGetDeviceConfig},
    {UsbComm::CMD_PUT_DEVICE_CONFIG, &handlePutDeviceConfig},
    {UsbComm::CMD_GET_INPUT_BINDINGS, &runGetInputBindings},
    {UsbComm::CMD_PUT_INPUT_BINDINGS, &handlePutInputBindings},
    {UsbComm::CMD_SCREEN_SETTINGS, &handleScreenSettings},
    {UsbComm::CMD_SET_BOARD_PROFILE, &handleSetBoardProfile},
    {UsbComm::CMD_OTA_BEGIN, &handleOtaBegin},
    {UsbComm::CMD_OTA_END, &handleOtaEnd},
    {UsbComm::CMD_CAN_SCAN_START, &runCanScanStart},
    {UsbComm::CMD_CAN_SCAN_STOP, &runCanScanStop},
    {UsbComm::CMD_OBD_READ_DTC, &runObdReadDtc},
    {UsbComm::CMD_OBD_CLEAR_DTC, &runObdClearDtc},
};

const UsbCommand *findUsbCommand(uint8_t code) {
    for (const UsbCommand &c : kUsbCommands) {
        if (c.code == code)
            return &c;
    }
    return nullptr;
}

void handleCommand(const char *jsonLine) {
    s_lastHostCmdMs = millis();
    LOG_VDEBUG("USB", "Received command: %.40s...", jsonLine);

    const size_t jsonLen = strlen(jsonLine);
    const uint8_t cmd = extractCmdByte(jsonLine);
    CmdHeapGuard heapGuard{cmd};

    if (consumeRawPayloadCommand(cmd, jsonLine, jsonLen))
        return;

    JsonDocument doc;
    if (!parseOrFail(jsonLine, jsonLen, doc, nullptr))
        return;

    const JsonObjectConst obj = doc.as<JsonObjectConst>();

    if (const DeviceCommands::Command *shared = DeviceCommands::findByUsbCode(cmd)) {
        renderSharedOutcome(shared->run(obj));
        return;
    }

    const UsbCommand *entry = findUsbCommand(cmd);
    if (entry == nullptr) {
        LOG_WARN("USB", "Unknown cmd: 0x%02X — replying unknown_command", cmd);
        UsbComm::sendError("unknown_command");
        return;
    }
    entry->run(obj);
}

} // namespace UsbCommInternal
