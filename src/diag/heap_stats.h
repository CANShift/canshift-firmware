#pragma once
// heap_stats.h — Periodic free-/largest-block sampler + USB emitter (issue
// #1369).
//
// Driven from `UsbComm::tick()` at the 20 ms rate; an internal counter gates
// the actual sample + emit cycle so the JSON line only goes out every
// HEAP_STATS_EMIT_INTERVAL_MS. No allocation on the path — payload is built
// into a stack buffer and pushed through `UsbComm::sendLine`.
//
// Wire format (tuner-side schema: `HeapStatsFrameSchema` in canshift-core):
//   {"heap_stats":1,"ts":<ms>,"free_int":<bytes>,"largest_int":<bytes>,
//    "free_psram":<bytes|null>,"largest_psram":<bytes|null>}
//
// PSRAM fields are `null` on WROOM builds (no PSRAM region installed). The
// tuner persists a short ring and surfaces the trace in the About panel.

#include <stdint.h>

namespace HeapStats {

struct Snapshot {
    uint32_t tsMs;
    uint32_t freeInternal;
    uint32_t largestInternal;
    uint32_t freePsram;    // 0 when PSRAM is absent — see `hasPsram`
    uint32_t largestPsram; // 0 when PSRAM is absent — see `hasPsram`
    bool hasPsram;
};

// Sample heap state right now without emitting. Useful at boot / before a
// known-large allocation so the post-event snapshot has a baseline.
Snapshot sampleNow();

// Last snapshot taken (whether by `tick` or `sampleNow`). Zero-initialised
// at boot until the first sample lands.
const Snapshot &latest();

// Per-tick entry point — call from `UsbComm::tick()` (20 ms rate). Internal
// counter decides when to sample + emit so callers stay rate-agnostic.
void tick();

// Force an immediate sample + emit, regardless of the internal interval.
// Used right after `FontManager::init` / first dashboard build so the host
// has a post-init baseline in the trace.
void emitNow();

} // namespace HeapStats
