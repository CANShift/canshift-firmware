#include "bench_can_tx.h"

#include "can/can_manager.h"
#include "hal/ble/ble_server.h"
#include "hal/memory/psram.h"
#include "hal/touch/touch_driver.h"

#include <cstdint>

namespace {

BenchCanTx::Sink s_txSink = nullptr;

} // namespace

namespace BenchCanTx {

void setSink(Sink sink) {
    s_txSink = sink;
}

void emit(uint32_t id, const uint8_t *data, uint8_t length) {
    if (s_txSink != nullptr)
        s_txSink(id, data, length);
}

} // namespace BenchCanTx

namespace CanManager {

uint32_t msSinceLastRx() {
    return 0;
}

uint32_t busRateHz() {
    return 0;
}

bool sendFrame(uint32_t id, const uint8_t *data, uint8_t length, bool) {
    BenchCanTx::emit(id, data, length);
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
