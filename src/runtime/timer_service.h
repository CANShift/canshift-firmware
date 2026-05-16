// canshift-firmware/src/runtime/timer_service.h
#pragma once
// timer_service.h — Single source of truth for the dashboard stopwatch.
//
// Owns the start / pause / resume / reset state. The on-device touch
// handler in `ui/widgets/timer_widget.cpp` drives the service through this
// API. The service has zero BLE dependency — it is fully usable from a sim
// build with `APP_BLE_ENABLED=0`.
//
// Threading model
// ----------------
// Every public method takes an internal FreeRTOS mutex.
//
// Time source: `esp_timer_get_time()` (microseconds since boot, monotonic).
//
// Lap capture was removed in #679: the API existed (lap buffer + callbacks)
// but no consumer ever drained it. Re-introduce alongside the consumer that
// needs it (phone-side chronos history, #285) rather than carrying dead BSS.

#include <cstdint>

namespace TimerService {

enum class State : uint8_t {
    Reset = 0,
    Running = 1,
    Paused = 2,
};

struct Snapshot {
    State state;
    uint32_t elapsedMs;
    uint32_t version; ///< Monotonic — bumps on every state change.
};

/// Initialize the service. Call once during boot, before any caller can
/// invoke the API. No-op on subsequent calls.
void init();

/// State transitions. Each returns `true` iff the state actually changed
/// (so callers can avoid emitting a redundant notify).
[[nodiscard]] bool start();
[[nodiscard]] bool pause();
[[nodiscard]] bool resume();
[[nodiscard]] bool reset();

/// Atomic snapshot of the public state.
Snapshot snapshot();
State getState();
uint32_t getElapsedMs();

} // namespace TimerService
