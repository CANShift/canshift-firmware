// signal_store.cpp — Thread-safe signal value store

#include "signal_store.h"
#include "diag/logger.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

static SignalStore::SignalValue s_signals[SIGNAL_STORE_MAX_SIGNALS];
static SemaphoreHandle_t s_mutex = nullptr;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

inline bool acquireLock() {
    return xSemaphoreTake(s_mutex, pdMS_TO_TICKS(5)) == pdTRUE;
}

inline void releaseLock() {
    xSemaphoreGive(s_mutex);
}

inline bool idValid(SignalId id) {
    return id < SIGNAL_STORE_MAX_SIGNALS;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void SignalStore::init() {
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        LOG_ERROR("STORE", "Failed to create signal store mutex");
        return;
    }

    // Initialize all signals to invalid state
    for (int i = 0; i < SIGNAL_STORE_MAX_SIGNALS; ++i) {
        s_signals[i] = {.raw = 0.0f,
                        .smoothed = 0.0f,
                        .lastUpdateMs = 0,
                        .valid = false,
                        .timeoutMs = SIGNAL_DEFAULT_TIMEOUT_MS};
    }

    LOG_INFO("STORE", "Signal store initialized (%d slots)", SIGNAL_STORE_MAX_SIGNALS);
}

void SignalStore::update(SignalId id, float value) {
    if (!idValid(id))
        return;

    if (!acquireLock()) {
        LOG_WARN("STORE", "update() lock timeout for signal %d", id);
        return;
    }

    SignalValue &sig = s_signals[id];

    // Apply EMA smoothing: smoothed = α * raw + (1-α) * smoothed
    if (sig.valid) {
        sig.smoothed = SIGNAL_EMA_ALPHA * value + (1.0f - SIGNAL_EMA_ALPHA) * sig.smoothed;
    } else {
        // First value — initialize smoothed to raw value
        sig.smoothed = value;
    }

    sig.raw = value;
    sig.lastUpdateMs = millis();
    sig.valid = true;

    releaseLock();
}

float SignalStore::read(SignalId id, float defaultValue) {
    if (!idValid(id))
        return defaultValue;

    if (!acquireLock())
        return defaultValue;

    float result = s_signals[id].valid ? s_signals[id].smoothed : defaultValue;
    releaseLock();
    return result;
}

float SignalStore::readRaw(SignalId id, float defaultValue) {
    if (!idValid(id))
        return defaultValue;

    if (!acquireLock())
        return defaultValue;

    float result = s_signals[id].valid ? s_signals[id].raw : defaultValue;
    releaseLock();
    return result;
}

bool SignalStore::isValid(SignalId id) {
    if (!idValid(id))
        return false;

    if (!acquireLock())
        return false;
    bool result = s_signals[id].valid;
    releaseLock();
    return result;
}

SignalStore::SignalValue SignalStore::get(SignalId id) {
    SignalValue copy = {};
    if (!idValid(id))
        return copy;

    if (!acquireLock())
        return copy;
    copy = s_signals[id];
    releaseLock();
    return copy;
}

void SignalStore::snapshotAll(SignalValue out[SIGNAL_STORE_MAX_SIGNALS]) {
    if (out == nullptr) return;

    if (!acquireLock()) {
        // Best-effort: zero out the buffer so callers see invalid signals
        // rather than reading uninitialized memory.
        for (int i = 0; i < SIGNAL_STORE_MAX_SIGNALS; ++i) {
            out[i] = SignalValue{};
        }
        return;
    }

    memcpy(out, s_signals, sizeof(SignalValue) * SIGNAL_STORE_MAX_SIGNALS);
    releaseLock();
}

void SignalStore::setTimeout(SignalId id, uint32_t timeoutMs) {
    if (!idValid(id))
        return;

    if (!acquireLock())
        return;
    s_signals[id].timeoutMs = timeoutMs;
    releaseLock();
}

void SignalStore::checkTimeouts() {
    uint32_t now = millis();

    if (!acquireLock())
        return;

    for (int i = 0; i < SIGNAL_STORE_MAX_SIGNALS; ++i) {
        if (s_signals[i].valid) {
            uint32_t age = now - s_signals[i].lastUpdateMs;
            if (age > s_signals[i].timeoutMs) {
                s_signals[i].valid = false;
                LOG_VDEBUG("STORE", "Signal %d timed out (age=%ums)", i, age);
            }
        }
    }

    releaseLock();
}
