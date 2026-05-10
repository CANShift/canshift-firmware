// canshift-firmware/src/runtime/timer_service.h
#pragma once
// timer_service.h — Single source of truth for the dashboard stopwatch.
//
// Owns the start / pause / resume / reset / lap state. The on-device touch
// handler in `ui/widgets/timer_widget.cpp` and (in PR2) the BLE GATT timer
// service both drive the same instance through this API, so dash and phone
// stay in lock-step. The service has zero BLE dependency — it is fully
// usable from a sim build with `APP_BLE_ENABLED=0`.
//
// Threading model
// ----------------
// Every public method takes an internal FreeRTOS mutex. Callbacks
// (`StateChangeCb`, `LapCb`) fire **inside** that lock — they MUST be
// non-blocking and lock-free (set an atomic flag, post a queue, etc.).
// Do NOT call back into TimerService from inside a callback (re-entrant
// deadlock).
//
// Time source: `esp_timer_get_time()` (microseconds since boot, monotonic).

#include <cstddef>
#include <cstdint>
#include <functional>

namespace TimerService {

enum class State : uint8_t {
    Reset = 0,
    Running = 1,
    Paused = 2,
};

struct Snapshot {
    State state;
    uint32_t elapsedMs;
    uint16_t lapCount;
    uint32_t version; ///< Monotonic — bumps on every state change AND every successful lap.
};

struct Lap {
    uint16_t index;             ///< 0-based lap index in the current run.
    uint32_t elapsedMs;         ///< Delta since previous lap (or since start for index 0).
    uint32_t totalMsAtLap;      ///< Total elapsed at the moment the lap was captured.
    uint64_t capturedUsSinceBoot; ///< Wall-clock-ish anchor for sequencing flushes.
};

/// Sentinel returned by `lap()` when the timer is not Running.
constexpr uint16_t kLapRejected = 0xFFFF;

/// Initialize the service. Call once during boot, before any caller can
/// invoke the API. No-op on subsequent calls.
void init();

/// State transitions. Each returns `true` iff the state actually changed
/// (so callers can avoid emitting a redundant notify).
[[nodiscard]] bool start();
[[nodiscard]] bool pause();
[[nodiscard]] bool resume();
[[nodiscard]] bool reset();

/// Capture a lap. Only valid in `Running` state.
/// @return Newly captured lap index, or `kLapRejected` when not Running.
uint16_t lap();

/// Atomic snapshot of the public state.
Snapshot snapshot();
State getState();
uint32_t getElapsedMs();

/// Buffered-lap drain — visitor receives laps in FIFO order. The visitor
/// returns `true` to confirm the lap was consumed (it is then popped from
/// the buffer); `false` to abort the drain leaving the lap in place.
/// @return Number of laps successfully drained.
using LapVisitor = std::function<bool(const Lap &)>;
size_t drainBufferedLaps(const LapVisitor &visit);

/// Number of laps currently held in the ring buffer.
size_t bufferedLapCount();

/// Callback hooks — fire under the service mutex. MUST be non-blocking.
/// State-change fires only when the `state` field of `Snapshot` actually
/// changes. Lap fires once per successful `lap()` call (touch- or
/// BLE-driven).
using StateChangeCb = void (*)();
using LapCb = void (*)(const Lap &);

void setOnStateChange(StateChangeCb cb);
void setOnLap(LapCb cb);

} // namespace TimerService
