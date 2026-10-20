#include "bus_scan.h"

#include "can_driver_install.h"
#include "diag/error_store.h"
#include "diag/logger.h"
#include "util/mem_alloc.h"

#include <Arduino.h>
#include <driver/twai.h>
#include <freertos/FreeRTOS.h>

#include <atomic>
#include <new>
#include <string.h>

namespace {

constexpr uint32_t kRxPollTimeoutMs = 10;

std::atomic<BusScanRs *> s_scan{nullptr};
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
std::atomic<bool> s_requested{false};
std::atomic<bool> s_running{false};
bool s_driverUp = false;

BusScanRs *scanState() {
    return s_scan.load(std::memory_order_acquire);
}

bool ensureScanState() {
    if (scanState() != nullptr)
        return true;
    void *block = Mem::allocPreferSpiram(sizeof(BusScanRs));
    if (block == nullptr) {
        LOG_ERROR("CAN", "Bus scan state allocation failed");
        ErrorStore::push(ERROR_SRC_CAN, "SCAN_ALLOC", "bus scan state allocation failed");
        return false;
    }
    s_scan.store(new (block) BusScanRs{}, std::memory_order_release);
    return true;
}

class ScanLock {
  public:
    ScanLock() {
        portENTER_CRITICAL(&s_mux);
    }
    ~ScanLock() {
        portEXIT_CRITICAL(&s_mux);
    }
    ScanLock(const ScanLock &) = delete;
    ScanLock &operator=(const ScanLock &) = delete;
};

uint8_t failScan() {
    const ScanLock lock;
    return bus_scan_fail_rs(scanState());
}

bool releaseDriver() {
    if (!s_driverUp)
        return true;
    s_driverUp = CanDriverInstall::stopOnThisCore() != ESP_OK;
    return !s_driverUp;
}

uint16_t installKbps() {
    const ScanLock lock;
    return scanState()->install_kbps;
}

bool installListenOnly() {
    if (!releaseDriver())
        return false;
    const uint16_t kbps = installKbps();
    s_driverUp =
        CanDriverInstall::startOnThisCore(CanDriverInstall::BusMode::ListenOnly, kbps) == ESP_OK;
    if (s_driverUp)
        LOG_INFO("CAN", "Bus scan listening at %u kbit/s", static_cast<unsigned>(kbps));
    return s_driverUp;
}

BusScan::Tick restoreDriver() {
    s_running.store(false, std::memory_order_release);
    const bool released = releaseDriver();
    const uint16_t kbps = CanDriverInstall::configuredSpeedKbps();
    if (released &&
        CanDriverInstall::startOnThisCore(CanDriverInstall::BusMode::Normal, kbps) == ESP_OK) {
        LOG_INFO("CAN", "Bus scan done — driver back at %u kbit/s", static_cast<unsigned>(kbps));
        return BusScan::Tick::Idle;
    }
    LOG_ERROR("CAN", "Bus scan could not restore the TWAI driver");
    ErrorStore::push(ERROR_SRC_CAN, "SCAN_RESTORE", "bus scan left the driver down");
    return BusScan::Tick::DriverLost;
}

BusScan::Tick perform(uint8_t action);

BusScan::Tick installOrFail() {
    if (installListenOnly())
        return BusScan::Tick::Idle;
    LOG_ERROR("CAN", "Bus scan listen-only install failed");
    return perform(failScan());
}

BusScan::Tick stayPut() {
    return BusScan::Tick::Idle;
}

using ActionHandler = BusScan::Tick (*)();

constexpr ActionHandler kActionHandlers[] = {stayPut, installOrFail, restoreDriver};

static_assert(sizeof(kActionHandlers) / sizeof(kActionHandlers[0]) == kBusScanActionRestore + 1,
              "every bus scan action needs a handler");

BusScan::Tick perform(uint8_t action) {
    if (action > kBusScanActionRestore)
        return BusScan::Tick::Idle;
    return kActionHandlers[action]();
}

uint32_t busErrorCount() {
    twai_status_info_t status;
    const esp_err_t err = twai_get_status_info(&status);
    if (err == ESP_OK)
        return status.bus_error_count;
    LOG_WARN("CAN", "Bus scan status read failed: %s", esp_err_to_name(err));
    return 0;
}

void abortBeforeListening() {
    LOG_ERROR("CAN", "Bus scan could not stop the driver — scan aborted");
    ErrorStore::push(ERROR_SRC_CAN, "SCAN_STOP", "bus scan could not stop the driver");
    const ScanLock lock;
    bus_scan_abort_rs(scanState());
}

BusScan::Tick beginScan() {
    s_requested.store(false, std::memory_order_release);
    if (CanDriverInstall::stopOnThisCore() != ESP_OK) {
        abortBeforeListening();
        return BusScan::Tick::Idle;
    }
    s_driverUp = false;
    s_running.store(true, std::memory_order_release);
    uint8_t action;
    {
        const ScanLock lock;
        action = bus_scan_begin_rs(scanState(), millis());
    }
    return perform(action);
}

bool receiveOne() {
    twai_message_t message;
    if (twai_receive(&message, pdMS_TO_TICKS(kRxPollTimeoutMs)) != ESP_OK)
        return false;
    const ScanLock lock;
    bus_scan_on_frame_rs(scanState(), message.identifier);
    return true;
}

BusScan::Tick runScan() {
    const bool frame = receiveOne();
    const uint32_t errors = busErrorCount();
    uint8_t action;
    {
        const ScanLock lock;
        action = bus_scan_tick_rs(scanState(), millis(), errors);
    }
    const BusScan::Tick outcome = perform(action);
    if (outcome != BusScan::Tick::Idle || !frame)
        return outcome;
    return BusScan::Tick::Frame;
}

}

bool BusScan::claimsBus() {
    return s_requested.load(std::memory_order_acquire) || s_running.load(std::memory_order_acquire);
}

bool BusScan::requestStart() {
    if (claimsBus() || !ensureScanState())
        return false;
    {
        const ScanLock lock;
        bus_scan_queue_rs(scanState());
    }
    s_requested.store(true, std::memory_order_release);
    return true;
}

BusScan::Tick BusScan::tick() {
    if (s_running.load(std::memory_order_acquire))
        return runScan();
    return beginScan();
}

void BusScan::copyReport(BusScanRs &out) {
    const BusScanRs *scan = scanState();
    if (scan == nullptr) {
        out = BusScanRs{};
        return;
    }
    const ScanLock lock;
    memcpy(&out, scan, sizeof(out));
}
