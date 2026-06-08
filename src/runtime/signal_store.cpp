// signal_store.cpp — Thread-safe signal value store
//
// CRITICAL-SECTION INVARIANT (issue #877):
// All portENTER_CRITICAL / portEXIT_CRITICAL pairs below guard
// s_signals against cross-core access. On ESP32 the IDF implementation
// is a spinlock plus IRQ-disable on the current core. Inside any such
// pair you MUST NOT:
//   - call LOG_* (can block / take another mutex / alloc),
//   - allocate (new/delete/malloc/free, String, snprintf to heap),
//   - take any other lock (mutex / semaphore / lv_lock).
// Violations deadlock or trip the IDF crit-section assert at runtime.

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
    // NO LOG / NO ALLOC / NO LOCK inside this critical section — see file header (#877).
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

    // Hold the lock across the entire read-modify-write so concurrent writers
    // to the same SignalId (CAN task on core 0 + optimistic UI write from a
    // button widget on core 1, sim engine, etc.) cannot clobber each other's
    // EMA result with a stale prevSmoothed snapshot. See #1008. FP math under
    // the spinlock is precedented by setTimeout / checkTimeouts below.
    // NO LOG / NO ALLOC / NO LOCK inside this critical section — see file header (#877).
    portENTER_CRITICAL(&s_signalsMux);
    SignalValue &sig = s_signals[id];
    const float new_smoothed =
        sig.valid ? (SIGNAL_EMA_ALPHA * value + (1.0f - SIGNAL_EMA_ALPHA) * sig.smoothed)
                  : value; // First value — initialize smoothed to raw value
    sig.raw = value;
    sig.smoothed = new_smoothed;
    sig.lastUpdateMs = now;
    sig.valid = true;
    portEXIT_CRITICAL(&s_signalsMux);
}

void SignalStore::set(SignalId id, float value) {
    if (!idValid(id))
        return;

    uint32_t now = millis();

    // Canonical overwrite — raw and smoothed both receive `value` verbatim,
    // bypassing the EMA. Used for synthetic toggle writes (#1285) where the
    // caller's value IS the truth, not a sample. Lock held for the whole
    // write so concurrent CAN updates can't interleave a stale smoothed.
    // NO LOG / NO ALLOC / NO LOCK inside this critical section — see file header (#877).
    portENTER_CRITICAL(&s_signalsMux);
    SignalValue &sig = s_signals[id];
    sig.raw = value;
    sig.smoothed = value;
    sig.lastUpdateMs = now;
    sig.valid = true;
    portEXIT_CRITICAL(&s_signalsMux);
}

float SignalStore::read(SignalId id, float defaultValue) {
    if (!idValid(id))
        return defaultValue;

    // NO LOG / NO ALLOC / NO LOCK inside this critical section — see file header (#877).
    portENTER_CRITICAL(&s_signalsMux);
    float result = s_signals[id].valid ? s_signals[id].smoothed : defaultValue;
    portEXIT_CRITICAL(&s_signalsMux);
    return result;
}

bool SignalStore::isValid(SignalId id) {
    if (!idValid(id))
        return false;

    // NO LOG / NO ALLOC / NO LOCK inside this critical section — see file header (#877).
    portENTER_CRITICAL(&s_signalsMux);
    bool result = s_signals[id].valid;
    portEXIT_CRITICAL(&s_signalsMux);
    return result;
}

bool SignalStore::anyValid(const SignalId *ids, size_t count) {
    if (ids == nullptr || count == 0)
        return false;

    // NO LOG / NO ALLOC / NO LOCK inside this critical section — see file header (#877).
    // `idValid()` is a pure compile-time bound check, so it's lock-safe here.
    portENTER_CRITICAL(&s_signalsMux);
    for (size_t i = 0; i < count; ++i) {
        const SignalId id = ids[i];
        if (idValid(id) && s_signals[id].valid) {
            portEXIT_CRITICAL(&s_signalsMux);
            return true;
        }
    }
    portEXIT_CRITICAL(&s_signalsMux);
    return false;
}

void SignalStore::snapshotAll(SignalValue out[SIGNAL_STORE_MAX_SIGNALS]) {
    if (out == nullptr)
        return;

    // NO LOG / NO ALLOC / NO LOCK inside this critical section — see file header (#877).
    portENTER_CRITICAL(&s_signalsMux);
    memcpy(out, s_signals, sizeof(SignalValue) * SIGNAL_STORE_MAX_SIGNALS);
    portEXIT_CRITICAL(&s_signalsMux);
}

void SignalStore::setTimeout(SignalId id, uint32_t timeoutMs) {
    if (!idValid(id))
        return;

    // NO LOG / NO ALLOC / NO LOCK inside this critical section — see file header (#877).
    portENTER_CRITICAL(&s_signalsMux);
    s_signals[id].timeoutMs = timeoutMs;
    portEXIT_CRITICAL(&s_signalsMux);
}

void SignalStore::checkTimeouts() {
    uint32_t now = millis();

    // NO LOG / NO ALLOC / NO LOCK inside this critical section — see file header (#877).
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
