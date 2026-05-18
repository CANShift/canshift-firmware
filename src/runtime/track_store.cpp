// track_store.cpp — Atomic latch for Track-mode telemetry (#844).
//
// Implementation mirrors `signal_store` — a portMUX spinlock around a fixed
// state struct. The critical section never allocates and never logs.

#include "track_store.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <string.h>

namespace TrackStore {

namespace {

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
State s_state = {};

} // namespace

void init() {
    portENTER_CRITICAL(&s_mux);
    memset(&s_state, 0, sizeof(s_state));
    portEXIT_CRITICAL(&s_mux);
}

void setTelemetry(const State &next) {
    const uint32_t nowMs = millis();
    portENTER_CRITICAL(&s_mux);
    s_state = next;
    s_state.lastUpdateMs = nowMs;
    portEXIT_CRITICAL(&s_mux);
}

void snapshot(State *out) {
    if (!out)
        return;
    portENTER_CRITICAL(&s_mux);
    *out = s_state;
    portEXIT_CRITICAL(&s_mux);
}

bool isActiveWithin(uint32_t timeoutMs) {
    portENTER_CRITICAL(&s_mux);
    const bool active = s_state.trackMode;
    const uint32_t last = s_state.lastUpdateMs;
    portEXIT_CRITICAL(&s_mux);
    if (!active)
        return false;
    return (millis() - last) <= timeoutMs;
}

} // namespace TrackStore
