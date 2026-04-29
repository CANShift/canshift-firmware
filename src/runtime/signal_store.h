#pragma once
// signal_store.h — Thread-safe central store for live engine signal values
//
// The SignalStore is the data bus between the CAN decode layer and the UI layer.
// CAN task writes decoded values.
// UI task reads values at render time.
// Thread safety is achieved with a FreeRTOS mutex.
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
     * Creates the mutex and sets all signals to invalid state.
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
     * Read the smoothed value of a signal. Thread-safe.
     * Returns the EMA-smoothed value, or defaultValue if signal is invalid/timed out.
     */
float read(SignalId id, float defaultValue = 0.0f);

/**
     * Read the raw (unsmoothed) value. Thread-safe.
     */
float readRaw(SignalId id, float defaultValue = 0.0f);

/**
     * Check if a signal is valid (received recently, within timeout).
     */
bool isValid(SignalId id);

/**
     * Get the full signal state. Thread-safe.
     * Copies the struct to avoid holding the lock during reads.
     */
SignalValue get(SignalId id);

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
