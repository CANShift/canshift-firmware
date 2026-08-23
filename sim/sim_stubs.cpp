#include "sim_can_bus.h"

#include "can/can_manager.h"
#include "hal/ble/ble_server.h"
#include "hal/memory/psram.h"
#include "hal/touch/touch_driver.h"
#include "hal/usb/usb_comm.h"

#include <Arduino.h>

#include <cstdint>
#include <cstdio>

namespace {
uint32_t s_lastRxMs = 0;
}

namespace SimCanBus {

void markRx() {
    s_lastRxMs = millis();
}

} // namespace SimCanBus

namespace CanManager {

uint32_t msSinceLastRx() {
    if (s_lastRxMs == 0)
        return UINT32_MAX;
    return millis() - s_lastRxMs;
}

uint32_t busRateHz() {
    return 842;
}

bool sendFrame(uint32_t id, const uint8_t *, uint8_t len, bool) {
    printf("[sim] CAN tx id=0x%03X len=%u\n", static_cast<unsigned>(id),
           static_cast<unsigned>(len));
    return true;
}

} // namespace CanManager

namespace TouchDriver {

void resetCalibration() {}

} // namespace TouchDriver

namespace canshift::hal::memory {

bool isPsramAvailable() {
#ifdef BOARD_HAS_PSRAM
    return true;
#else
    return false;
#endif
}

} // namespace canshift::hal::memory

namespace UsbComm {

void sendLine(const char *line) {
    printf("[sim] usb: %s\n", line);
}

} // namespace UsbComm

namespace BleServer {

bool isEnabled() {
    return true;
}

void setPendingEnabled(bool) {}

bool isConnected() {
    return false;
}

} // namespace BleServer
