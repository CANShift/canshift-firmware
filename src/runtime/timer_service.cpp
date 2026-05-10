// canshift-firmware/src/runtime/timer_service.cpp
// Implements TimerService — see header for the threading contract.

#include "timer_service.h"

#include "diag/logger.h"

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

// Bounded buffer holds laps captured while no consumer (phone) is connected.
// 32 ≈ a full track session; overflow drops oldest and warns once.
constexpr size_t TIMER_LAP_BUFFER_CAPACITY = 32;

// Mutex acquisition timeout — long enough to cover render-task contention
// without freezing the touch handler if something else holds the lock.
constexpr TickType_t TIMER_LOCK_TIMEOUT = pdMS_TO_TICKS(10);

// ---------------------------------------------------------------------------
// Internal state — single static instance, no globals exposed in the header.
// ---------------------------------------------------------------------------

struct ServiceState {
    SemaphoreHandle_t mutex = nullptr;

    TimerService::State state = TimerService::State::Reset;
    int64_t lastStartUs = 0;   ///< esp_timer_get_time() at last start/resume.
    int64_t accumulatedUs = 0; ///< Frozen elapsed at last pause.
    uint16_t lapCount = 0;
    uint32_t version = 0;

    TimerService::Lap lapBuffer[TIMER_LAP_BUFFER_CAPACITY] = {};
    size_t bufHead = 0; ///< Pop position (oldest).
    size_t bufTail = 0; ///< Push position (next slot).
    size_t bufCount = 0;

    TimerService::StateChangeCb onStateChange = nullptr;
    TimerService::LapCb onLap = nullptr;

    bool initialized = false;
};

ServiceState g_state;

// ---------------------------------------------------------------------------
// Lock helpers — RAII keeps every public path symmetric, even on early exit.
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Private helpers — caller MUST already hold g_state.mutex.
// ---------------------------------------------------------------------------

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

void fireStateChangeLocked() {
    bumpVersionLocked();
    if (g_state.onStateChange)
        g_state.onStateChange();
}

void pushLapLocked(const TimerService::Lap &lap) {
    if (g_state.bufCount == TIMER_LAP_BUFFER_CAPACITY) {
        // Drop oldest and warn — losing the head preserves recent laps which
        // are usually more interesting to a session-in-progress.
        g_state.bufHead = (g_state.bufHead + 1) % TIMER_LAP_BUFFER_CAPACITY;
        g_state.bufCount--;
        LOG_WARN("TIMER", "Lap buffer overflow — dropped oldest entry (capacity=%u)",
                 static_cast<unsigned>(TIMER_LAP_BUFFER_CAPACITY));
    }
    g_state.lapBuffer[g_state.bufTail] = lap;
    g_state.bufTail = (g_state.bufTail + 1) % TIMER_LAP_BUFFER_CAPACITY;
    g_state.bufCount++;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

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
    g_state.lapCount = 0;
    g_state.version = 0;
    g_state.bufHead = 0;
    g_state.bufTail = 0;
    g_state.bufCount = 0;
    g_state.onStateChange = nullptr;
    g_state.onLap = nullptr;
    g_state.initialized = true;

    LOG_INFO("TIMER", "TimerService initialized (lap buffer capacity=%u)",
             static_cast<unsigned>(TIMER_LAP_BUFFER_CAPACITY));
}

bool TimerService::start() {
    LockGuard lk(g_state.mutex);
    if (!lk.held())
        return false;

    if (g_state.state == State::Running)
        return false;

    // From Reset OR Paused: begin counting from now.
    // From Reset, accumulatedUs is already 0; from Paused we resume below
    // via a separate path, so `start()` semantically restarts a fresh run
    // when called from Paused (mirrors how the old widget behaved on tap).
    // To keep semantics tight, only allow Reset -> Running here. From Paused
    // callers must use `resume()`.
    if (g_state.state != State::Reset)
        return false;

    g_state.lastStartUs = esp_timer_get_time();
    g_state.accumulatedUs = 0;
    g_state.state = State::Running;
    fireStateChangeLocked();
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
    fireStateChangeLocked();
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
    fireStateChangeLocked();
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
    g_state.lapCount = 0;
    // Buffered laps belong to the run we just discarded — drop them too.
    g_state.bufHead = 0;
    g_state.bufTail = 0;
    g_state.bufCount = 0;

    if (wasNonReset) {
        fireStateChangeLocked();
        LOG_DEBUG("TIMER", "reset()");
    }
    return wasNonReset;
}

uint16_t TimerService::lap() {
    LockGuard lk(g_state.mutex);
    if (!lk.held())
        return kLapRejected;

    if (g_state.state != State::Running)
        return kLapRejected;

    const uint32_t totalMs = elapsedMsLocked();
    // Per-lap delta is "totalMs since previous lap" — for index 0 that's
    // simply totalMs (since start).
    uint32_t deltaMs = totalMs;
    if (g_state.lapCount > 0 && g_state.bufCount > 0) {
        // Find the previous lap by walking back from tail. We always have
        // the *most recent* lap in the buffer until something drains it.
        const size_t prevIdx =
            (g_state.bufTail + TIMER_LAP_BUFFER_CAPACITY - 1) % TIMER_LAP_BUFFER_CAPACITY;
        const uint32_t prevTotal = g_state.lapBuffer[prevIdx].totalMsAtLap;
        deltaMs = (totalMs >= prevTotal) ? (totalMs - prevTotal) : 0;
    }

    Lap captured{};
    captured.index = g_state.lapCount;
    captured.elapsedMs = deltaMs;
    captured.totalMsAtLap = totalMs;
    captured.capturedUsSinceBoot = static_cast<uint64_t>(esp_timer_get_time());

    pushLapLocked(captured);
    g_state.lapCount++;
    bumpVersionLocked();

    if (g_state.onLap)
        g_state.onLap(captured);
    LOG_DEBUG("TIMER", "lap %u total=%ums delta=%ums", static_cast<unsigned>(captured.index),
              static_cast<unsigned>(captured.totalMsAtLap),
              static_cast<unsigned>(captured.elapsedMs));
    return captured.index;
}

TimerService::Snapshot TimerService::snapshot() {
    Snapshot snap{};
    LockGuard lk(g_state.mutex);
    if (!lk.held())
        return snap;

    snap.state = g_state.state;
    snap.elapsedMs = elapsedMsLocked();
    snap.lapCount = g_state.lapCount;
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

size_t TimerService::drainBufferedLaps(const LapVisitor &visit) {
    if (!visit)
        return 0;

    LockGuard lk(g_state.mutex);
    if (!lk.held())
        return 0;

    size_t drained = 0;
    while (g_state.bufCount > 0) {
        const Lap &front = g_state.lapBuffer[g_state.bufHead];
        if (!visit(front))
            break;
        g_state.bufHead = (g_state.bufHead + 1) % TIMER_LAP_BUFFER_CAPACITY;
        g_state.bufCount--;
        drained++;
    }
    return drained;
}

size_t TimerService::bufferedLapCount() {
    LockGuard lk(g_state.mutex);
    if (!lk.held())
        return 0;
    return g_state.bufCount;
}

void TimerService::setOnStateChange(StateChangeCb cb) {
    LockGuard lk(g_state.mutex);
    if (!lk.held())
        return;
    g_state.onStateChange = cb;
}

void TimerService::setOnLap(LapCb cb) {
    LockGuard lk(g_state.mutex);
    if (!lk.held())
        return;
    g_state.onLap = cb;
}
