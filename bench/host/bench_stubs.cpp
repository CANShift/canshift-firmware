#include "can/can_manager.h"
#include "hal/ble/ble_server.h"
#include "hal/memory/psram.h"
#include "hal/touch/touch_driver.h"

#include <cstdint>

namespace CanManager {

uint32_t msSinceLastRx() {
    return 0;
}

uint32_t busRateHz() {
    return 0;
}

bool sendFrame(uint32_t, const uint8_t *, uint8_t, bool) {
    return true;
}

} // namespace CanManager

namespace TouchDriver {

void resetCalibration() {}

} // namespace TouchDriver

namespace canshift::hal::memory {

bool isPsramAvailable() {
    return true;
}

} // namespace canshift::hal::memory

namespace BleServer {

bool isEnabled() {
    return false;
}

void requestStop() {}

bool isConnected() {
    return false;
}

} // namespace BleServer
