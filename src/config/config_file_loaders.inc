
#include "config_loader_internal.h"

#include "app_config.h"
#include "board_config.h"
#include "config/parse_utils.h"
#include "diag/error_store.h"
#include "diag/logger.h"
#include "hal/storage/storage_driver.h"

#include "json_reader.h"

#include <ArduinoJson.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

namespace ConfigLoaderInternal {

bool loadDevice() {
    if (!StorageDriver::fileExists(CONFIG_PATH_DEVICE)) {
        LOG_INFO("CFG", "device.json not found — using board_config.h defaults");
        return false;
    }

    logLargestFreeBlock(CONFIG_PATH_DEVICE);

    JsonDocument doc(JsonReader::configAllocator());
    DeserializationError err = StorageDriver::parseJsonFile(CONFIG_PATH_DEVICE, doc);
    if (err) {
        LOG_WARN("CFG", "device.json parse error: %s — using defaults", err.c_str());
        return false;
    }

    CfgDeviceConfig parsed{};
    parsed.canSpeedKbps = doc["can_speed_kbps"] | 0;
    parsed.twaiTxPin = doc["twai_tx_pin"] | -1;
    parsed.twaiRxPin = doc["twai_rx_pin"] | -1;
    parsed.loaded = true;
    s_device = parsed;
    LOG_INFO("CFG", "device.json loaded: CAN=%ukbps TX=GPIO%d RX=GPIO%d", s_device.canSpeedKbps,
             s_device.twaiTxPin, s_device.twaiRxPin);
    return true;
}

bool loadInputBindings() {
    s_inputs.count = 0;
    s_inputs.loaded = false;
    memset(s_inputs.bindings, 0, sizeof(s_inputs.bindings));

    if (!StorageDriver::fileExists(CONFIG_PATH_INPUTS)) {
        LOG_INFO("CFG", "input_bindings.json not found — no physical buttons configured");
        return false;
    }

    JsonDocument doc(JsonReader::configAllocator());
    DeserializationError err = StorageDriver::parseJsonFile(CONFIG_PATH_INPUTS, doc);
    if (err) {
        LOG_WARN("CFG", "input_bindings.json parse error: %s — ignoring file", err.c_str());
        return false;
    }

    JsonArrayConst arr = doc["input_bindings"].as<JsonArrayConst>();
    if (arr.isNull()) {
        LOG_WARN("CFG", "input_bindings.json missing 'input_bindings' array — ignoring file");
        return false;
    }

    for (JsonObjectConst entry : arr) {
        if (s_inputs.count >= CFG_MAX_INPUT_BINDINGS) {
            LOG_WARN("CFG", "input_bindings.json: > %u entries — extras dropped",
                     static_cast<unsigned>(CFG_MAX_INPUT_BINDINGS));
            break;
        }
        CfgInputBinding &b = s_inputs.bindings[s_inputs.count];
        memset(&b, 0, sizeof(b));
        strlcpy(b.id, entry["id"] | "", sizeof(b.id));
        const int pin = entry["pin"] | -1;
        if (pin < 0 || pin > 39) {
            LOG_WARN("CFG", "input binding '%s': pin=%d out of range — dropped", b.id, pin);
            continue;
        }
        if (isPinConflict(static_cast<int8_t>(pin))) {
            LOG_WARN("CFG", "input binding '%s': pin=%d conflicts with TWAI — dropped", b.id, pin);
            continue;
        }
        b.pin = static_cast<int8_t>(pin);
        b.active = parseInputActive(entry["active"] | "low");
        b.pullup = entry["pullup"] | true;
        int debounce = entry["debounce_ms"] | 20;
        if (debounce < 1)
            debounce = 1;
        if (debounce > 500)
            debounce = 500;
        b.debounceMs = static_cast<uint16_t>(debounce);
        b.kind = parseInputPressKind(entry["kind"] | "short");
        JsonObjectConst action = entry["action"].as<JsonObjectConst>();
        if (action.isNull()) {
            LOG_WARN("CFG", "input binding '%s': missing 'action' — dropped", b.id);
            continue;
        }
        parseButtonAction(action, &b.action);
        if (b.action.type == CfgButtonActionType::UNKNOWN) {
            LOG_WARN("CFG", "input binding '%s': unknown / invalid action — dropped", b.id);
            continue;
        }
        strlcpy(b.signal, entry["signal"] | "", sizeof(b.signal));
        s_inputs.count++;
    }

    s_inputs.loaded = true;
    LOG_INFO("CFG", "input_bindings.json loaded: %u bindings", s_inputs.count);
    return true;
}

} // namespace ConfigLoaderInternal
