#include "app_config.h"
#if APP_BLE_ENABLED

    #include "ble_json_arena.h"
    #include "ble_json_budget.h"
    #include "ble_server_internal.h"

    #include "config/json_reader.h"
    #include "config/rotation_config.h"
    #include "diag/logger.h"
    #include "diag/lvgl_hold_timer.h"
    #include "runtime/device_commands.h"
    #include "runtime/lvgl_lock.h"
    #include "runtime/sound_settings.h"
    #include "runtime/timer_service.h"
    #include "ui/settings_page.h"
    #include "util/json_writer.h"

    #include <ArduinoJson.h>
    #include <Arduino.h>
    #include <NimBLEDevice.h>

namespace {

constexpr size_t kBleMaxWriteLen = BleJsonBudget::kMaxWriteBytes;
constexpr uint32_t kBleSettingsMutexTimeoutMs = 50U;
constexpr char kTooLongReply[] = "{\"err\":\"too_long\"}";
constexpr char kBusyReply[] = "{\"err\":\"busy\"}";
constexpr char kInvalidSoundReply[] = "{\"err\":\"invalid_sound_settings\"}";

struct TimerOp {
    int code;
    bool (*apply)();
};

constexpr TimerOp kTimerOps[] = {
    {1, TimerService::start}, {2, TimerService::pause}, {3, TimerService::resume},
    {4, TimerService::reset}, {5, TimerService::lap},
};

bool admitWrite(NimBLECharacteristic *pChar, const std::string &val, const char *label,
                bool replyTooLong) {
    if (val.empty())
        return false;
    if (val.length() <= kBleMaxWriteLen)
        return true;
    LOG_WARN("BLE", "%s write %u bytes exceeds cap %u — dropping", label,
             static_cast<unsigned>(val.length()), static_cast<unsigned>(kBleMaxWriteLen));
    if (replyTooLong)
        pChar->setValue(kTooLongReply);
    return false;
}

bool parseInto(JsonDocument &doc, const std::string &val) {
    return JsonReader::parse(doc, val.c_str(), val.length()) == DeserializationError::Ok;
}

bool applyBrightness(NimBLECharacteristic *pChar, JsonVariantConst brightnessVar) {
    if (brightnessVar.isNull())
        return true;
    const uint8_t brightness = brightnessVar.as<uint8_t>();
    LvglLock lock(pdMS_TO_TICKS(kBleSettingsMutexTimeoutMs));
    if (!lock.held()) {
        LOG_WARN("BLE", "SETTINGS write: LVGL mutex busy — brightness dropped");
        pChar->setValue(kBusyReply);
        return false;
    }
    LVGL_HOLD_TIMER(::PerfCounters::MUTEX_HOLD_BLE);
    SettingsPage::applyFromUsb(brightness);
    LOG_DEBUG("BLE", "Settings applied via BLE");
    return true;
}

void applyRotation(JsonVariantConst rotationVar) {
    if (rotationVar.isNull())
        return;
    const uint16_t rotation = rotationVar.as<uint16_t>();
    if ((rotation == 0 || rotation == 180) && rotation != RotationConfig::getOffsetDeg()) {
        LOG_INFO("BLE", "Rotation change requested: %u° — rebooting", rotation);
        RotationConfig::applyAndReboot(rotation);
    }
}

const TimerOp *timerOpFor(int code) {
    for (const TimerOp &op : kTimerOps) {
        if (op.code == code)
            return &op;
    }
    return nullptr;
}

class SettingsCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *pChar) override {
        const std::string val = pChar->getValue();
        if (!admitWrite(pChar, val, "SETTINGS", true))
            return;
        BleJsonArena::LeasedDocument leased(BleJsonArena::Slot::Callback);
        JsonDocument &doc = leased.document();
        if (!parseInto(doc, val))
            return;
        if (!applyBrightness(pChar, doc["brightness"]))
            return;
        if (!SoundSettings::apply(doc.as<JsonObjectConst>())) {
            pChar->setValue(kInvalidSoundReply);
            return;
        }
        applyRotation(doc["rotation"]);
    }

    void onRead(NimBLECharacteristic *pChar) override {
        BleJsonArena::LeasedDocument leased(BleJsonArena::Slot::Callback);
        JsonDocument &doc = leased.document();
        doc["brightness"] = SettingsPage::getBrightness();
        if (SoundSettings::available())
            SoundSettings::writeTo(doc.as<JsonObject>());
        char buf[128];
        const size_t len = JsonWriter::toBuffer(doc, buf, sizeof(buf));
        if (doc.overflowed() || len == 0 || len >= sizeof(buf)) {
            LOG_WARN("BLE", "SETTINGS read payload truncated (len=%u, cap=%u) — emitting canary",
                     static_cast<unsigned>(len), static_cast<unsigned>(sizeof(buf)));
            pChar->setValue(kTooLongReply);
            return;
        }
        pChar->setValue(buf);
    }
};

class CmdCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *pChar) override {
        const std::string val = pChar->getValue();
        if (!admitWrite(pChar, val, "CMD", false))
            return;
        BleJsonArena::LeasedDocument leased(BleJsonArena::Slot::Callback);
        JsonDocument &doc = leased.document();
        if (!parseInto(doc, val))
            return;
        const char *cmd = doc["cmd"] | "";
        const DeviceCommands::Command *entry = DeviceCommands::findByBleName(cmd);
        if (entry == nullptr) {
            LOG_WARN("BLE", "Unknown CMD: %s", cmd);
            return;
        }
        if (entry->run(doc.as<JsonObjectConst>()) == DeviceCommands::Outcome::RebootRequested) {
            delay(kPreRestartFlushDelayMs);
            esp_restart();
        }
    }
};

class TimerCmdCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *pChar) override {
        const std::string val = pChar->getValue();
        if (!admitWrite(pChar, val, "TIMER cmd", false))
            return;
        BleJsonArena::LeasedDocument leased(BleJsonArena::Slot::Callback);
        JsonDocument &doc = leased.document();
        if (!parseInto(doc, val))
            return;
        JsonVariantConst opVar = doc["op"];
        if (opVar.isNull() || !opVar.is<int>()) {
            LOG_WARN("BLE", "TIMER cmd missing 'op' int — ignoring");
            return;
        }
        const int code = opVar.as<int>();
        const TimerOp *op = timerOpFor(code);
        if (op == nullptr) {
            LOG_WARN("BLE", "Unknown TIMER op: %d", code);
            return;
        }
        const bool accepted = op->apply();
        LOG_DEBUG("BLE", "TIMER op %d %s", code, accepted ? "applied" : "rejected");
    }
};

}

NimBLECharacteristicCallbacks &BleServerInternal::settingsCallbacks() {
    static SettingsCallbacks s_callbacks;
    return s_callbacks;
}

NimBLECharacteristicCallbacks &BleServerInternal::cmdCallbacks() {
    static CmdCallbacks s_callbacks;
    return s_callbacks;
}

NimBLECharacteristicCallbacks &BleServerInternal::timerCmdCallbacks() {
    static TimerCmdCallbacks s_callbacks;
    return s_callbacks;
}

#endif
