// signal_store.cpp — Thread-safe signal value store

#include "signal_store.h"
#include "diag/logger.h"

#if defined(ARDUINO) || defined(ESP_PLATFORM)
    #include <Arduino.h>
    #include <freertos/FreeRTOS.h>
    #include <freertos/portmacro.h>
#else
// Native test env (no FreeRTOS): the harness is single-threaded so the
// critical-section macros become no-ops. portMUX_TYPE is reduced to a
// tag so the existing call sites compile unchanged. See PR fixing
// post-#752 native test build break.
typedef int portMUX_TYPE;
    #define portMUX_INITIALIZER_UNLOCKED 0
    #define portENTER_CRITICAL(mux) ((void)(mux))
    #define portEXIT_CRITICAL(mux) ((void)(mux))
#endif
#include <string.h>

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

static SignalStore::SignalValue s_signals[SIGNAL_STORE_MAX_SIGNALS];
static portMUX_TYPE s_signalsMux = portMUX_INITIALIZER_UNLOCKED;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

inline bool idValid(SignalId id) {
    return id < SIGNAL_STORE_MAX_SIGNALS;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void SignalStore::init() {
    // Initialize all signals to invalid state
    portENTER_CRITICAL(&s_signalsMux);
    for (int i = 0; i < SIGNAL_STORE_MAX_SIGNALS; ++i) {
        s_signals[i] = {.raw = 0.0f,
                        .smoothed = 0.0f,
                        .lastUpdateMs = 0,
                        .valid = false,
                        .timeoutMs = SIGNAL_DEFAULT_TIMEOUT_MS};
    }
    portEXIT_CRITICAL(&s_signalsMux);

    LOG_INFO("STORE", "Signal store initialized (%d slots)", SIGNAL_STORE_MAX_SIGNALS);
}

void SignalStore::update(SignalId id, float value) {
    if (!idValid(id))
        return;

    uint32_t now = millis();

    portENTER_CRITICAL(&s_signalsMux);

    SignalValue &sig = s_signals[id];

    // Apply EMA smoothing: smoothed = α * raw + (1-α) * smoothed
    if (sig.valid) {
        sig.smoothed = SIGNAL_EMA_ALPHA * value + (1.0f - SIGNAL_EMA_ALPHA) * sig.smoothed;
    } else {
        // First value — initialize smoothed to raw value
        sig.smoothed = value;
    }

    sig.raw = value;
    sig.lastUpdateMs = now;
    sig.valid = true;

    portEXIT_CRITICAL(&s_signalsMux);
}

float SignalStore::read(SignalId id, float defaultValue) {
    if (!idValid(id))
        return defaultValue;

    portENTER_CRITICAL(&s_signalsMux);
    float result = s_signals[id].valid ? s_signals[id].smoothed : defaultValue;
    portEXIT_CRITICAL(&s_signalsMux);
    return result;
}

float SignalStore::readRaw(SignalId id, float defaultValue) {
    if (!idValid(id))
        return defaultValue;

    portENTER_CRITICAL(&s_signalsMux);
    float result = s_signals[id].valid ? s_signals[id].raw : defaultValue;
    portEXIT_CRITICAL(&s_signalsMux);
    return result;
}

bool SignalStore::isValid(SignalId id) {
    if (!idValid(id))
        return false;

    portENTER_CRITICAL(&s_signalsMux);
    bool result = s_signals[id].valid;
    portEXIT_CRITICAL(&s_signalsMux);
    return result;
}

SignalStore::SignalValue SignalStore::get(SignalId id) {
    SignalValue copy = {};
    if (!idValid(id))
        return copy;

    portENTER_CRITICAL(&s_signalsMux);
    copy = s_signals[id];
    portEXIT_CRITICAL(&s_signalsMux);
    return copy;
}

void SignalStore::snapshotAll(SignalValue out[SIGNAL_STORE_MAX_SIGNALS]) {
    if (out == nullptr)
        return;

    portENTER_CRITICAL(&s_signalsMux);
    memcpy(out, s_signals, sizeof(SignalValue) * SIGNAL_STORE_MAX_SIGNALS);
    portEXIT_CRITICAL(&s_signalsMux);
}

void SignalStore::setTimeout(SignalId id, uint32_t timeoutMs) {
    if (!idValid(id))
        return;

    portENTER_CRITICAL(&s_signalsMux);
    s_signals[id].timeoutMs = timeoutMs;
    portEXIT_CRITICAL(&s_signalsMux);
}

void SignalStore::checkTimeouts() {
    uint32_t now = millis();

    portENTER_CRITICAL(&s_signalsMux);
    for (int i = 0; i < SIGNAL_STORE_MAX_SIGNALS; ++i) {
        if (s_signals[i].valid) {
            uint32_t age = now - s_signals[i].lastUpdateMs;
            if (age > s_signals[i].timeoutMs) {
                s_signals[i].valid = false;
            }
        }
    }
    portEXIT_CRITICAL(&s_signalsMux);
}
