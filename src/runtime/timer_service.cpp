#include "timer_service.h"

#include "diag/logger.h"
#include "timer_core_rs.h"

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {

constexpr TickType_t TIMER_LOCK_TIMEOUT = pdMS_TO_TICKS(10);

struct ServiceState {
    SemaphoreHandle_t mutex = nullptr;
    TimerCoreState core = {};
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

TimerService::Lap toLap(const TimerCoreLap &raw) {
    return TimerService::Lap{raw.index, raw.session, raw.lap_ms, raw.total_ms};
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

    timer_core_init_rs(&g_state.core);
    g_state.initialized = true;

    LOG_INFO("TIMER", "TimerService initialized");
}

bool TimerService::start() {
    LockGuard lk(g_state.mutex);
    if (!lk.held())
        return false;

    if (!timer_core_start_rs(&g_state.core, esp_timer_get_time()))
        return false;
    LOG_DEBUG("TIMER", "start() — session %u", static_cast<unsigned>(g_state.core.session));
    return true;
}

bool TimerService::pause() {
    LockGuard lk(g_state.mutex);
    if (!lk.held())
        return false;

    const int64_t nowUs = esp_timer_get_time();
    if (!timer_core_pause_rs(&g_state.core, nowUs))
        return false;
    LOG_DEBUG("TIMER", "pause() at %ums",
              static_cast<unsigned>(timer_core_elapsed_ms_rs(&g_state.core, nowUs)));
    return true;
}

bool TimerService::resume() {
    LockGuard lk(g_state.mutex);
    if (!lk.held())
        return false;

    if (!timer_core_resume_rs(&g_state.core, esp_timer_get_time()))
        return false;
    LOG_DEBUG("TIMER", "resume()");
    return true;
}

bool TimerService::reset() {
    LockGuard lk(g_state.mutex);
    if (!lk.held())
        return false;

    if (!timer_core_reset_rs(&g_state.core))
        return false;
    LOG_DEBUG("TIMER", "reset()");
    return true;
}

bool TimerService::lap() {
    LockGuard lk(g_state.mutex);
    if (!lk.held())
        return false;

    TimerCoreLap raw = {};
    bool droppedOldest = false;
    if (!timer_core_lap_rs(&g_state.core, esp_timer_get_time(), &raw, &droppedOldest))
        return false;

    if (droppedOldest) {
        LOG_WARN("TIMER", "Lap buffer full (%u) — dropped oldest unsynced lap",
                 static_cast<unsigned>(TIMER_CORE_LAP_CAPACITY));
    }
    LOG_DEBUG("TIMER", "lap %u — %ums (total %ums)", static_cast<unsigned>(raw.index),
              static_cast<unsigned>(raw.lap_ms), static_cast<unsigned>(raw.total_ms));
    return true;
}

TimerService::Snapshot TimerService::snapshot() {
    Snapshot snap{};
    LockGuard lk(g_state.mutex);
    if (!lk.held())
        return snap;

    snap.state = static_cast<State>(g_state.core.run_state);
    snap.elapsedMs = timer_core_elapsed_ms_rs(&g_state.core, esp_timer_get_time());
    snap.version = g_state.core.version;
    snap.lapCount = g_state.core.lap_count;
    snap.sessionId = g_state.core.session;
    return snap;
}

TimerService::State TimerService::getState() {
    LockGuard lk(g_state.mutex);
    if (!lk.held())
        return State::Reset;
    return static_cast<State>(g_state.core.run_state);
}

uint32_t TimerService::getElapsedMs() {
    LockGuard lk(g_state.mutex);
    if (!lk.held())
        return 0;
    return timer_core_elapsed_ms_rs(&g_state.core, esp_timer_get_time());
}

uint8_t TimerService::pendingLapCount() {
    LockGuard lk(g_state.mutex);
    if (!lk.held())
        return 0;
    return timer_core_pending_count_rs(&g_state.core);
}

bool TimerService::popPendingLap(Lap &out) {
    LockGuard lk(g_state.mutex);
    if (!lk.held())
        return false;

    TimerCoreLap raw = {};
    if (!timer_core_pop_pending_rs(&g_state.core, &raw))
        return false;
    out = toLap(raw);
    return true;
}
