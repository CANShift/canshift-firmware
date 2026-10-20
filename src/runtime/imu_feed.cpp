#include "imu_feed.h"

#include "util/mem_alloc.h"

#include <Arduino.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <new>

namespace ImuFeed {

namespace {

struct Snapshot {
    ImuCoreState core;
    LaunchTimer launch;
    uint32_t publishedMs;
    bool published;
};

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
Mem::HeapPtr<Snapshot> s_snapshot;
std::atomic<bool> s_present{false};
std::atomic<bool> s_peakResetRequested{false};
std::atomic<bool> s_levelRequested{false};
std::atomic<uint8_t> s_levelStatus{kImuLevelPending};

bool freshSnapshot(Snapshot &out) {
    if (!present())
        return false;
    portENTER_CRITICAL(&s_mux);
    out = *s_snapshot;
    portEXIT_CRITICAL(&s_mux);
    return out.published && millis() - out.publishedMs <= kStaleAfterMs;
}

}

bool reserve() {
    if (s_snapshot)
        return true;
    void *raw = Mem::allocPreferSpiram(sizeof(Snapshot));
    if (!raw)
        return false;
    s_snapshot.reset(new (raw) Snapshot{});
    return true;
}

void markPresent() {
    s_present.store(s_snapshot != nullptr, std::memory_order_release);
}

bool present() {
    return s_present.load(std::memory_order_acquire);
}

void publish(const ImuCoreState &core, const LaunchTimer &launch) {
    if (!present())
        return;
    const uint32_t nowMs = millis();
    portENTER_CRITICAL(&s_mux);
    s_snapshot->core = core;
    s_snapshot->launch = launch;
    s_snapshot->publishedMs = nowMs;
    s_snapshot->published = true;
    portEXIT_CRITICAL(&s_mux);
}

bool latest(ImuCoreState &out) {
    Snapshot snapshot;
    if (!freshSnapshot(snapshot))
        return false;
    out = snapshot.core;
    return true;
}

bool latestLaunch(LaunchTimer &out) {
    Snapshot snapshot;
    if (!freshSnapshot(snapshot))
        return false;
    out = snapshot.launch;
    return true;
}

void requestPeakReset() {
    s_peakResetRequested.store(true, std::memory_order_release);
}

bool takePeakReset() {
    return s_peakResetRequested.exchange(false, std::memory_order_acq_rel);
}

void requestLevel() {
    s_levelStatus.store(kImuLevelPending, std::memory_order_release);
    s_levelRequested.store(true, std::memory_order_release);
}

bool takeLevelRequest() {
    return s_levelRequested.exchange(false, std::memory_order_acq_rel);
}

void reportLevel(uint8_t status) {
    s_levelStatus.store(status, std::memory_order_release);
}

uint8_t levelStatus() {
    return s_levelStatus.load(std::memory_order_acquire);
}

}
