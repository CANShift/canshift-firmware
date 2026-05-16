// canshift-firmware/src/runtime/timer_service.cpp
// Implements TimerService — see header for the threading contract.

#include "timer_service.h"

#include "diag/logger.h"

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {

// Mutex acquisition timeout — long enough to cover render-task contention
// without freezing the touch handler if something else holds the lock.
constexpr TickType_t TIMER_LOCK_TIMEOUT = pdMS_TO_TICKS(10);

struct ServiceState {
    SemaphoreHandle_t mutex = nullptr;

    TimerService::State state = TimerService::State::Reset;
    int64_t lastStartUs = 0;   ///< esp_timer_get_time() at last start/resume.
    int64_t accumulatedUs = 0; ///< Frozen elapsed at last pause.
    uint32_t version = 0;

    bool initialized = false;
};

ServiceState g_state;

class LockGuard {
  public:
    explicit LockGuard(SemaphoreHandle_t m) : mutex_(m) {
        held_ = mutex_ && (xSemaphoreTake(mutex_, TIMER_LOCK_TIMEOUT) == pdTRUE);
    }
    ~LockGuard() {
        if (held_)
            xSemaphoreGive(mutex_);
    }
    LockGuard(const LockGuard &) = delete;
    LockGuard &operator=(const LockGuard &) = delete;
    [[nodiscard]] bool held() const {
        return held_;
    }

  private:
    SemaphoreHandle_t mutex_;
    bool held_ = false;
};

// Private helpers — caller MUST already hold g_state.mutex.

int64_t elapsedUsLocked() {
    if (g_state.state == TimerService::State::Running) {
        return g_state.accumulatedUs + (esp_timer_get_time() - g_state.lastStartUs);
    }
    return g_state.accumulatedUs;
}

uint32_t elapsedMsLocked() {
    int64_t us = elapsedUsLocked();
    if (us < 0)
        us = 0;
    return static_cast<uint32_t>(us / 1000);
}

void bumpVersionLocked() {
    g_state.version++;
}

} // namespace

void TimerService::init() {
    if (g_state.initialized)
        return;

    g_state.mutex = xSemaphoreCreateMutex();
    if (!g_state.mutex) {
        LOG_ERROR("TIMER", "Failed to create timer service mutex");
        return;
    }

    g_state.state = State::Reset;
    g_state.lastStartUs = 0;
    g_state.accumulatedUs = 0;
    g_state.version = 0;
    g_state.initialized = true;

    LOG_INFO("TIMER", "TimerService initialized");
}

bool TimerService::start() {
    LockGuard lk(g_state.mutex);
    if (!lk.held())
        return false;

    if (g_state.state != State::Reset)
        return false;

    g_state.lastStartUs = esp_timer_get_time();
    g_state.accumulatedUs = 0;
    g_state.state = State::Running;
    bumpVersionLocked();
    LOG_DEBUG("TIMER", "start()");
    return true;
}

bool TimerService::pause() {
    LockGuard lk(g_state.mutex);
    if (!lk.held())
        return false;

    if (g_state.state != State::Running)
        return false;

    g_state.accumulatedUs += (esp_timer_get_time() - g_state.lastStartUs);
    g_state.state = State::Paused;
    bumpVersionLocked();
    LOG_DEBUG("TIMER", "pause() at %ums", static_cast<unsigned>(elapsedMsLocked()));
    return true;
}

bool TimerService::resume() {
    LockGuard lk(g_state.mutex);
    if (!lk.held())
        return false;

    if (g_state.state != State::Paused)
        return false;

    g_state.lastStartUs = esp_timer_get_time();
    g_state.state = State::Running;
    bumpVersionLocked();
    LOG_DEBUG("TIMER", "resume()");
    return true;
}

bool TimerService::reset() {
    LockGuard lk(g_state.mutex);
    if (!lk.held())
        return false;

    const bool wasNonReset = (g_state.state != State::Reset);

    g_state.state = State::Reset;
    g_state.lastStartUs = 0;
    g_state.accumulatedUs = 0;

    if (wasNonReset) {
        bumpVersionLocked();
        LOG_DEBUG("TIMER", "reset()");
    }
    return wasNonReset;
}

TimerService::Snapshot TimerService::snapshot() {
    Snapshot snap{};
    LockGuard lk(g_state.mutex);
    if (!lk.held())
        return snap;

    snap.state = g_state.state;
    snap.elapsedMs = elapsedMsLocked();
    snap.version = g_state.version;
    return snap;
}

TimerService::State TimerService::getState() {
    LockGuard lk(g_state.mutex);
    if (!lk.held())
        return State::Reset;
    return g_state.state;
}

uint32_t TimerService::getElapsedMs() {
    LockGuard lk(g_state.mutex);
    if (!lk.held())
        return 0;
    return elapsedMsLocked();
}
