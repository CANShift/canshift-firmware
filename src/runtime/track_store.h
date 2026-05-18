#pragma once
// track_store.h — Thread-safe latch for Track-mode telemetry pushed over BLE
// by canshift-mobile. Issue #844 (consumer of the core TrackTelemetrySchema
// from #843).
//
// Concurrency: writers are BLE-task `onWrite` callbacks; the UI task reads
// every render tick. A portMUX spinlock protects the small fixed-size state
// — critical sections are pure memcpy, no allocation, no logging.
//
// Staleness model: every write stamps `lastUpdateMs`. `get()` callers may
// consult `isStale(timeoutMs)` to decide whether the latched values are
// still trustworthy — once the BLE link drops, the indicator clears after
// the configured grace window even though no explicit "trackMode=false"
// arrived.

#include <stdbool.h>
#include <stdint.h>

namespace TrackStore {

/** Snapshot of the latest mobile-side Track-mode state. */
struct State {
    bool trackMode;
    uint32_t currentLapMs;
    uint32_t lastLapMs;
    uint32_t bestLapMs;
    uint16_t lapNumber;
    int32_t deltaMs;
    bool isBestLap;
    uint32_t lastUpdateMs; // millis() at the write
};

/** Initialise the internal spinlock + state. Call once at boot. */
void init();

/**
 * Atomically replace the latched state. The caller is expected to have
 * validated the BLE payload upstream — this routine performs no schema
 * checks of its own. `lastUpdateMs` is set internally from `millis()`.
 */
void setTelemetry(const State &next);

/** Copy the current latched state. Thread-safe. */
void snapshot(State *out);

/**
 * Convenience for the TopBar render path — returns the current trackMode
 * flag with an applied staleness gate so the badge clears automatically
 * after `timeoutMs` of silence from the mobile peer.
 */
bool isActiveWithin(uint32_t timeoutMs);

} // namespace TrackStore
