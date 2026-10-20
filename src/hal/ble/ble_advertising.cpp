#include "app_config.h"
#if APP_BLE_ENABLED

    #include "ble_server_internal.h"

    #include "ble_link_rs.h"
    #include "diag/error_store.h"
    #include "diag/logger.h"

    #include <Arduino.h>
    #include <NimBLEDevice.h>
    #include <atomic>
    #include <esp_bt.h>

namespace BleServerInternal {

namespace {

constexpr uint16_t kFastIntervalMinUnits = 48;
constexpr uint16_t kFastIntervalMaxUnits = 96;
constexpr uint16_t kSlowIntervalMinUnits = 668;
constexpr uint16_t kSlowIntervalMaxUnits = 700;

constexpr esp_power_level_t kConnectionTxPower = ESP_PWR_LVL_P9;
constexpr esp_power_level_t kAdvertisingTxPower = ESP_PWR_LVL_P3;

std::atomic<uint32_t> s_advertisingSinceMs{0};
std::atomic<bool> s_backedOff{false};

void setCadence(NimBLEAdvertising *adv, uint16_t minUnits, uint16_t maxUnits) {
    adv->setMinInterval(minUnits);
    adv->setMaxInterval(maxUnits);
}

bool restartAtSlowCadence() {
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    setCadence(adv, kSlowIntervalMinUnits, kSlowIntervalMaxUnits);
    if (!adv->isAdvertising())
        return true;
    return adv->stop() && adv->start();
}

}

void applyTxPower() {
    const esp_err_t defaultErr = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, kConnectionTxPower);
    const esp_err_t advErr = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, kAdvertisingTxPower);
    if (defaultErr == ESP_OK && advErr == ESP_OK)
        return;
    LOG_ERROR("BLE", "TX power refused (default=%s adv=%s)", esp_err_to_name(defaultErr),
              esp_err_to_name(advErr));
    ErrorStore::push(ERROR_SRC_SYSTEM, "ble_tx_power", "BLE TX power refused");
}

bool startAdvertisingFast() {
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    setCadence(adv, kFastIntervalMinUnits, kFastIntervalMaxUnits);
    s_advertisingSinceMs.store(millis(), std::memory_order_relaxed);
    s_backedOff.store(false, std::memory_order_relaxed);
    if (adv->start())
        return true;
    LOG_ERROR("BLE", "advertising start failed");
    ErrorStore::push(ERROR_SRC_SYSTEM, "ble_adv", "BLE advertising start failed");
    return false;
}

void paceAdvertising() {
    if (!s_enabled || !s_gattInited || s_backedOff.load(std::memory_order_relaxed))
        return;
    const uint32_t advertisingMs = millis() - s_advertisingSinceMs.load(std::memory_order_relaxed);
    if (!ble_link_advertising_backs_off_rs(advertisingMs))
        return;
    s_backedOff.store(true, std::memory_order_relaxed);
    if (restartAtSlowCadence() || s_connected) {
        LOG_INFO("BLE", "No client after %lus — advertising backed off",
                 static_cast<unsigned long>(advertisingMs / 1000u));
        return;
    }
    LOG_ERROR("BLE", "advertising backoff restart failed");
    ErrorStore::push(ERROR_SRC_SYSTEM, "ble_adv", "BLE advertising backoff failed");
}

}

#endif
