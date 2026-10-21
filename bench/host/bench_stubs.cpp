#include "bench_can_tx.h"

#include "can/bus_scan.h"
#include "boot/boot_sequence.h"
#include "can/can_manager.h"
#include "hal/ble/ble_server.h"
#include "hal/memory/psram.h"
#include "hal/touch/touch_driver.h"

#include <cstdint>

namespace {

BenchCanTx::Sink s_txSink = nullptr;

}

namespace BenchCanTx {

void setSink(Sink sink) {
    s_txSink = sink;
}

void emit(uint32_t id, const uint8_t *data, uint8_t length) {
    if (s_txSink != nullptr)
        s_txSink(id, data, length);
}

}

namespace CanManager {

bool isAvailable() {
    return false;
}

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

}

namespace BusScan {

bool requestStart() {
    return false;
}

void copyReport(BusScanRs &out) {
    out = BusScanRs{};
}

}

namespace TouchDriver {

void resetCalibration() {}

}

namespace canshift::hal::memory {

bool isPsramAvailable() {
    return true;
}

}

namespace BleServer {

bool isEnabled() {
    return false;
}

void requestStop() {}

bool isConnected() {
    return false;
}

}

namespace BootSequence {

void markOtaSlotValidIfPending() {}

}
