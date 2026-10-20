#include "usb_comm_internal.h"

#include "app_config.h"
#include "board.h"
#include "board_config.h"
#include "config/board_profile_store.h"
#include "config/json_reader.h"
#include "config/rotation_config.h"
#include "diag/logger.h"
#include "hal/usb/usb_seq_echo.h"
#include "hal/usb/usb_status_line.h"
#include "diag/lvgl_hold_timer.h"
#include "runtime/lvgl_lock.h"
#include "runtime/device_commands.h"
#include "runtime/config_reload_status.h"
#include "runtime/pending_actions.h"
#include "ui/settings_page.h"
#include "ui/theme_manager.h"
#include "util/json_writer.h"

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

void storeBoardProfileBlob(const JsonVariantConst &payload) {
    char blob[1536];
    const size_t n = JsonWriter::toBuffer(payload, blob, sizeof blob);
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

void storeCatalogBoard(const char *boardId) {
    if (!BoardProfileStore::saveBoardId(boardId)) {
        UsbComm::sendError("unknown_board_id");
        return;
    }
    LOG_INFO("USB", "CMD_SET_BOARD_PROFILE — catalog board '%s' selected, restarting", boardId);
    UsbComm::sendLine("{\"status\":\"ok\",\"restart\":true}");
    UsbComm::flushAndRestart();
}

void handleSetBoardProfile(const JsonObjectConst &doc) {
    JsonVariantConst payload = doc["payload"];
    if (!payload.is<JsonObjectConst>()) {
        UsbComm::sendError("missing_payload");
        return;
    }
    if (payload["magic"].is<const char *>()) {
        storeBoardProfileBlob(payload);
        return;
    }
    storeCatalogBoard(payload["board_id"] | "");
}

void sendStatusResponse() {
    char resp[160];
    const UsbStatusLine::Fields fields = {
        APP_VERSION_STR, static_cast<unsigned>(kUsbProtocolVersion), ThemeManager::isDayMode(),
        canshift::boards::runtimeBoardProfile().board_id};
    if (UsbStatusLine::format(fields, resp, sizeof(resp)) == 0) {
        LOG_ERROR("USB", "GET_STATUS payload does not fit %u B",
                  static_cast<unsigned>(sizeof(resp)));
        UsbComm::sendError("status_too_long");
        return;
    }
    UsbComm::sendLine(resp);
}

void sendPingResponse() {
    char resp[96];
    const int n = snprintf(resp, sizeof(resp),
                           "{\"status\":\"ok\",\"uptime_ms\":%lu,\"config_reload\":\"%s\"}",
                           static_cast<unsigned long>(millis()), ConfigReloadStatus::wireName());
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(resp)) {
        UsbComm::sendError("status_too_long");
        return;
    }
    UsbComm::sendLine(resp);
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
        LvglLock lock(pdMS_TO_TICKS(kUsbScreenSettingsMutexTimeoutMs));
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
            UsbComm::flushIfHostActive();
            RotationConfig::applyAndReboot(rotation);
        }
    }

    UsbComm::sendOk();
}

}

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

uint8_t extractCmdByte(const char *jsonLine, const char **afterCmd) {
    *afterCmd = nullptr;
    const char *cmdKey = strstr(jsonLine, "\"cmd\":");
    if (cmdKey == nullptr)
        return 0;
    char *endPtr = nullptr;
    const long parsed = strtol(cmdKey + 6, &endPtr, 10);
    if (endPtr == cmdKey + 6 || parsed < 0 || parsed > 0xFF)
        return 0;
    *afterCmd = endPtr;
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

bool consumeRawPayloadCommand(uint8_t cmd, char *jsonLine, size_t jsonLen) {
    if (cmd == UsbComm::kCmdPutConfig) {
        handlePutConfig(jsonLine);
        return true;
    }
    if (cmd == UsbComm::kCmdOtaWrite) {
        handleOtaWriteRaw(jsonLine);
        return true;
    }
    if (cmd != UsbComm::kCmdPutFile) {
        return false;
    }
    JsonDocument doc;
    if (parseOrFail(jsonLine, jsonLen, doc, "parse_error")) {
        handlePutFile(doc.as<JsonObjectConst>(), {jsonLine, jsonLen + 1});
    }
    return true;
}

struct UsbCommand {
    uint8_t code;
    void (*run)(const JsonObjectConst &);
};

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
    sendTypedConfigGet(kConfigPathDevice, TypedConfigBody::kDeviceConfigKey,
                       TypedConfigBody::device);
}
void runGetInputBindings(const JsonObjectConst &) {
    sendTypedConfigGet(kConfigPathInputs, TypedConfigBody::kInputBindingsKey,
                       TypedConfigBody::inputBindings);
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
    {UsbComm::kCmdGetStatus, &runGetStatus},
    {UsbComm::kCmdPing, &runPing},
    {UsbComm::kCmdGetConfig, &runGetConfig},
    {UsbComm::kCmdGetDeviceConfig, &runGetDeviceConfig},
    {UsbComm::kCmdPutDeviceConfig, &handlePutDeviceConfig},
    {UsbComm::kCmdGetInputBindings, &runGetInputBindings},
    {UsbComm::kCmdPutInputBindings, &handlePutInputBindings},
    {UsbComm::kCmdScreenSettings, &handleScreenSettings},
    {UsbComm::kCmdSetBoardProfile, &handleSetBoardProfile},
    {UsbComm::kCmdPutConfigChunk, &handlePutConfigChunk},
    {UsbComm::kCmdOtaBegin, &handleOtaBegin},
    {UsbComm::kCmdOtaEnd, &handleOtaEnd},
    {UsbComm::kCmdCanScanStart, &runCanScanStart},
    {UsbComm::kCmdCanScanStop, &runCanScanStop},
    {UsbComm::kCmdObdReadDtc, &runObdReadDtc},
    {UsbComm::kCmdObdClearDtc, &runObdClearDtc},
};

const UsbCommand *findUsbCommand(uint8_t code) {
    for (const UsbCommand &c : kUsbCommands) {
        if (c.code == code)
            return &c;
    }
    return nullptr;
}

void handleCommand(char *jsonLine) {
    LOG_VDEBUG("USB", "Received command: %.40s...", jsonLine);

    const size_t jsonLen = strlen(jsonLine);
    const char *afterCmd = nullptr;
    const uint8_t cmd = extractCmdByte(jsonLine, &afterCmd);
    CmdHeapGuard heapGuard{cmd};
    const UsbSeqEcho::Scope seqScope(UsbSeqEcho::parseAfterCmd(afterCmd));

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

}
