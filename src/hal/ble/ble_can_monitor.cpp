#include "app_config.h"
#if APP_BLE_ENABLED

    #include "ble_server_internal.h"

    #include "can/can_monitor.h"
    #include "can_monitor_rs.h"
    #include "diag/logger.h"

    #include <Arduino.h>
    #include <NimBLEDevice.h>
    #include <stddef.h>
    #include <stdint.h>
    #include <string>

namespace {

constexpr uint8_t kCanMonitorNotifiesPerTick = 2;
constexpr uint16_t kAttNotifyHeaderBytes = 3;

bool s_startRefused = false;

size_t notifyCapacity() {
    NimBLEServer *server = NimBLEDevice::getServer();
    if (server == nullptr || server->getConnectedCount() == 0)
        return 0;
    const uint16_t mtu = server->getPeerMTU(server->getPeerInfo(0).getConnHandle());
    if (mtu <= kAttNotifyHeaderBytes)
        return 0;
    const size_t payload = mtu - kAttNotifyHeaderBytes;
    return payload < kCanMonitorMaxFrameBytes ? payload : kCanMonitorMaxFrameBytes;
}

bool syncActivation(size_t subscribers) {
    if (subscribers == 0) {
        s_startRefused = false;
        if (CanMonitor::active())
            CanMonitor::stop();
        return false;
    }
    if (CanMonitor::active())
        return true;
    if (s_startRefused)
        return false;
    s_startRefused = !CanMonitor::start();
    return !s_startRefused;
}

void notifyPending(NimBLECharacteristic *pChar) {
    const size_t cap = notifyCapacity();
    uint8_t buf[kCanMonitorMaxFrameBytes];
    for (uint8_t sent = 0; sent < kCanMonitorNotifiesPerTick; sent++) {
        const size_t len = CanMonitor::encode(millis(), buf, cap);
        if (len == 0)
            return;
        pChar->setValue(buf, len);
        pChar->notify();
    }
}

class CanMonitorCallbacks : public BleServerInternal::AuthenticatedSubscribeCallbacks {
    void onWrite(NimBLECharacteristic *pChar) override {
        const std::string val = pChar->getValue();
        const auto *bytes = reinterpret_cast<const uint8_t *>(val.data());
        if (CanMonitor::setFilter(bytes, val.size())) {
            LOG_INFO("BLE", "CAN monitor filter set (%u bytes)", static_cast<unsigned>(val.size()));
            return;
        }
        LOG_WARN("BLE", "CAN monitor filter rejected (%u bytes)",
                 static_cast<unsigned>(val.size()));
    }
};

}

namespace BleServerInternal {

void emitCanMonitor() {
    auto *pMonitor = BleServerInternal::s_pCanMonitor;
    if (!pMonitor)
        return;
    if (!syncActivation(pMonitor->getSubscribedCount()))
        return;
    notifyPending(pMonitor);
}

void stopCanMonitor() {
    s_startRefused = false;
    CanMonitor::stop();
}

NimBLECharacteristicCallbacks &canMonitorCallbacks() {
    static CanMonitorCallbacks s_callbacks;
    return s_callbacks;
}

}

#endif
