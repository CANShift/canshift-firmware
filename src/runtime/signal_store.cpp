
#include "signal_store.h"
#include "diag/logger.h"

#if defined(ARDUINO) || defined(ESP_PLATFORM)
    #include <Arduino.h>
    #include <freertos/FreeRTOS.h>
    #include <freertos/portmacro.h>
#else
typedef int portMUX_TYPE;
    #define portMUX_INITIALIZER_UNLOCKED 0
    #define portENTER_CRITICAL(mux) ((void)(mux))
    #define portEXIT_CRITICAL(mux) ((void)(mux))
#endif
#include <atomic>
#include <string.h>

static SignalStore::SignalValue s_signals[SIGNAL_STORE_MAX_SIGNALS];
static portMUX_TYPE s_signalsMux = portMUX_INITIALIZER_UNLOCKED;
static std::atomic<bool> s_anyValid{false};

namespace {

inline bool idValid(SignalId id) {
    return id < SIGNAL_STORE_MAX_SIGNALS;
}

} // namespace

void SignalStore::init() {

    portENTER_CRITICAL(&s_signalsMux);
    for (int i = 0; i < SIGNAL_STORE_MAX_SIGNALS; ++i) {
        s_signals[i] = {.raw = 0.0f,
                        .smoothed = 0.0f,
                        .lastUpdateMs = 0,
                        .valid = false,
                        .timeoutMs = SIGNAL_DEFAULT_TIMEOUT_MS};
    }
    portEXIT_CRITICAL(&s_signalsMux);
    s_anyValid.store(false, std::memory_order_relaxed);

    LOG_INFO("STORE", "Signal store initialized (%d slots)", SIGNAL_STORE_MAX_SIGNALS);
}

void SignalStore::update(SignalId id, float value) {
    if (!idValid(id))
        return;

    uint32_t now = millis();

    portENTER_CRITICAL(&s_signalsMux);
    SignalValue &sig = s_signals[id];
    const float new_smoothed =
        sig.valid ? (SIGNAL_EMA_ALPHA * value + (1.0f - SIGNAL_EMA_ALPHA) * sig.smoothed) : value;
    sig.raw = value;
    sig.smoothed = new_smoothed;
    sig.lastUpdateMs = now;
    sig.valid = true;
    portEXIT_CRITICAL(&s_signalsMux);
    s_anyValid.store(true, std::memory_order_relaxed);
}

void SignalStore::set(SignalId id, float value) {
    if (!idValid(id))
        return;

    uint32_t now = millis();

    portENTER_CRITICAL(&s_signalsMux);
    SignalValue &sig = s_signals[id];
    sig.raw = value;
    sig.smoothed = value;
    sig.lastUpdateMs = now;
    sig.valid = true;
    portEXIT_CRITICAL(&s_signalsMux);
    s_anyValid.store(true, std::memory_order_relaxed);
}

float SignalStore::read(SignalId id, float defaultValue) {
    if (!idValid(id))
        return defaultValue;

    portENTER_CRITICAL(&s_signalsMux);
    float result = s_signals[id].valid ? s_signals[id].smoothed : defaultValue;
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

bool SignalStore::anyValid(const SignalId *ids, size_t count) {
    if (ids == nullptr || count == 0)
        return false;

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
    if (!s_anyValid.load(std::memory_order_relaxed))
        return;

    uint32_t now = millis();

    bool stillValid = false;
    portENTER_CRITICAL(&s_signalsMux);
    for (int i = 0; i < SIGNAL_STORE_MAX_SIGNALS; ++i) {
        if (s_signals[i].valid) {
            uint32_t age = now - s_signals[i].lastUpdateMs;
            if (age > s_signals[i].timeoutMs) {
                s_signals[i].valid = false;
            } else {
                stillValid = true;
            }
        }
    }
    portEXIT_CRITICAL(&s_signalsMux);
    s_anyValid.store(stillValid, std::memory_order_relaxed);
}
