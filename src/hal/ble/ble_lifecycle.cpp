#include "app_config.h"
#if APP_BLE_ENABLED

    #include "ble_server.h"
    #include "ble_server_internal.h"

    #include "diag/error_store.h"
    #include "diag/logger.h"

    #include <NimBLEDevice.h>
    #include <atomic>
    #if APP_WORKSHOP_ENABLED
        #include <esp_bt.h>
    #endif

using BleServerInternal::s_enabled;
using BleServerInternal::s_gattInited;
using BleServerInternal::s_stackInited;

static std::atomic<bool> s_stopRequested{false};
    #if APP_WORKSHOP_ENABLED
static std::atomic<bool> s_releaseRequested{false};
    #endif

namespace {

void teardownStack() {
    BleServerInternal::s_pTele = nullptr;
    BleServerInternal::s_pStatus = nullptr;
    BleServerInternal::s_pTimerState = nullptr;
    BleServerInternal::s_pTimerLap = nullptr;
    BleServerInternal::s_connected = false;

    std::atomic_thread_fence(std::memory_order_release);
    NimBLEDevice::deinit(true);
    s_enabled = false;
    s_stackInited = false;
    s_gattInited = false;
}

} // namespace

void BleServer::stop() {
    if (!s_enabled)
        return;
    if (!s_gattInited) {
        teardownStack();
        LOG_INFO("BLE", "BLE stack stopped — heap freed");
        return;
    }
    NimBLEDevice::stopAdvertising();
    BleServerInternal::s_connected = false;
    s_enabled = false;
    LOG_INFO("BLE", "BLE advertising stopped (GATT preserved)");
}

    #if APP_WORKSHOP_ENABLED

void BleServer::release() {
    if (BleServerInternal::s_released)
        return;
    if (s_stackInited)
        teardownStack();
    BleServerInternal::s_released = true;
    const esp_err_t err = esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);
    if (err != ESP_OK) {
        LOG_ERROR("BLE", "Controller memory release failed: %s", esp_err_to_name(err));
        ErrorStore::push(ERROR_SRC_SYSTEM, "ble_release", "BLE memory not released");
        return;
    }
    LOG_INFO("BLE", "BLE stopped for workshop mode — controller memory released");
}

    #endif

bool BleServer::isEnabled() {
    return s_enabled;
}

void BleServer::requestStop() {
    s_stopRequested.store(true, std::memory_order_relaxed);
}

bool BleServer::takeStopRequest() {
    return s_stopRequested.exchange(false, std::memory_order_relaxed);
}

    #if APP_WORKSHOP_ENABLED

void BleServer::requestRelease() {
    s_releaseRequested.store(true, std::memory_order_relaxed);
}

bool BleServer::takeReleaseRequest() {
    return s_releaseRequested.exchange(false, std::memory_order_relaxed);
}

    #endif

#endif
