#pragma once
// signal_store.h — Thread-safe central store for live engine signal values
//
// The SignalStore is the data bus between the CAN decode layer and the UI layer.
// CAN task writes decoded values.
// UI task reads values at render time.
//
// Thread safety (see #675):
//   - Access is serialized with a portMUX_TYPE spinlock, not a FreeRTOS mutex.
//   - The spinlock disables IRQs on the entering core; critical sections must
//     stay short and must not perform blocking calls (no logging, no malloc,
//     no FS/CAN I/O) while held.
//
// Each signal has:
//   - Current float value
//   - Timestamp of last update (millis())
//   - Valid flag (false if not yet received or timed out)
//
// Smoothing:
//   Gauge-type signals are optionally smoothed with an Exponential Moving Average.
//   Raw values are always stored alongside smoothed values.
//   The UI chooses whether to read raw or smoothed.

#include <stdint.h>
#include <stdbool.h>
#include "can/signal_map.h"
#include "app_config.h"

namespace SignalStore {

struct SignalValue {
    float raw;             // Latest decoded value (no filtering)
    float smoothed;        // EMA-filtered value (for gauges)
    uint32_t lastUpdateMs; // millis() when last updated
    bool valid;            // False if never received or timed out
    uint32_t timeoutMs;    // Configurable timeout per signal
};

/**
     * Initialize the signal store.
     * Initializes the spinlock and sets all signals to invalid state.
     */
void init();

/**
     * Update a signal value. Thread-safe.
     * Called by CAN parser and simulation engine.
     *
     * @param id    Signal ID (see signal_map.h)
     * @param value New decoded value
     */
void update(SignalId id, float value);

/**
     * Overwrite both raw and smoothed value, bypassing EMA. Thread-safe.
     * Use when the caller is the canonical source of truth (synthetic
     * toggle writes from physical button bindings, test fixtures), not a
     * sample to be smoothed. See #1285 for the motivating bug — feeding
     * smoothed reads back through update() left button toggles stuck.
     *
     * @param id    Signal ID (see signal_map.h)
     * @param value Canonical value — stored verbatim in both raw and smoothed.
     */
void set(SignalId id, float value);

/**
     * Read the smoothed value of a signal. Thread-safe.
     * Returns the EMA-smoothed value, or defaultValue if signal is invalid/timed out.
     */
[[nodiscard]] float read(SignalId id, float defaultValue = 0.0f);

/**
     * Check if a signal is valid (received recently, within timeout).
     */
[[nodiscard]] bool isValid(SignalId id);

/**
     * Bulk-copy every signal slot under one mutex acquisition. Designed for the
     * UI render path: WidgetFactory::updateAll() takes the lock once per frame
     * via this API instead of once per widget × value (issue #95). The output
     * buffer must hold SIGNAL_STORE_MAX_SIGNALS entries.
     */
void snapshotAll(SignalValue out[SIGNAL_STORE_MAX_SIGNALS]);

/**
     * Set per-signal timeout. Must be called before the CAN task starts.
     * Default: SIGNAL_DEFAULT_TIMEOUT_MS from app_config.h
     */
void setTimeout(SignalId id, uint32_t timeoutMs);

/**
     * Check all signals for timeout. Mark timed-out signals as invalid.
     * Call from the UI task or a periodic timer at ~100ms intervals.
     */
void checkTimeouts();

} // namespace SignalStore
