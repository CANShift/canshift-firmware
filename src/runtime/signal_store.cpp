
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
#include <cmath>
#include <string.h>

static SignalStore::SignalValue s_signals[kSignalStoreMaxSignals];
static portMUX_TYPE s_signalsMux = portMUX_INITIALIZER_UNLOCKED;
static std::atomic<bool> s_anyValid{false};
static std::atomic<bool> s_nonFiniteReported{false};

namespace {

inline bool idValid(SignalId id) {
    return id < kSignalStoreMaxSignals;
}

bool acceptsValue(SignalId id, float value) {
    if (!idValid(id))
        return false;
    if (std::isfinite(value))
        return true;
    if (!s_nonFiniteReported.exchange(true, std::memory_order_relaxed))
        LOG_WARN("STORE", "Non-finite value for signal %u dropped (reported once)",
                 static_cast<unsigned>(id));
    return false;
}

} // namespace

void SignalStore::init() {

    portENTER_CRITICAL(&s_signalsMux);
    for (int i = 0; i < kSignalStoreMaxSignals; ++i) {
        s_signals[i] = {
            .raw = 0.0f, .lastUpdateMs = 0, .valid = false, .timeoutMs = kSignalDefaultTimeoutMs};
    }
    portEXIT_CRITICAL(&s_signalsMux);
    s_anyValid.store(false, std::memory_order_relaxed);

    LOG_INFO("STORE", "Signal store initialized (%d slots)", kSignalStoreMaxSignals);
}

void SignalStore::update(SignalId id, float value) {
    if (!acceptsValue(id, value))
        return;

    uint32_t now = millis();

    portENTER_CRITICAL(&s_signalsMux);
    SignalValue &sig = s_signals[id];
    sig.raw = value;
    sig.lastUpdateMs = now;
    sig.valid = true;
    portEXIT_CRITICAL(&s_signalsMux);
    s_anyValid.store(true, std::memory_order_relaxed);
}

float SignalStore::read(SignalId id, float defaultValue) {
    if (!idValid(id))
        return defaultValue;

    portENTER_CRITICAL(&s_signalsMux);
    const float result = s_signals[id].valid ? s_signals[id].raw : defaultValue;
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

bool SignalStore::tryRead(SignalId id, float *value) {
    if (!idValid(id) || value == nullptr)
        return false;

    portENTER_CRITICAL(&s_signalsMux);
    const bool valid = s_signals[id].valid;
    *value = s_signals[id].raw;
    portEXIT_CRITICAL(&s_signalsMux);
    return valid;
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

void SignalStore::snapshotAll(SignalValue out[kSignalStoreMaxSignals]) {
    if (out == nullptr)
        return;

    portENTER_CRITICAL(&s_signalsMux);
    memcpy(out, s_signals, sizeof(SignalValue) * kSignalStoreMaxSignals);
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
    for (int i = 0; i < kSignalStoreMaxSignals; ++i) {
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
