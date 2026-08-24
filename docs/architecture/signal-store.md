# SignalStore

Source: [`src/runtime/signal_store.cpp`](../../src/runtime/signal_store.cpp)

SignalStore is the firmware's shared snapshot of every decoded CAN signal +
synthesized derived values. It is written by taskCAN (and the OBD-II
poller, and the button widget's optimistic-write path) and read by
taskUI, taskUSB (telemetry), taskBLE (telemetry), and the alert engine.
Sources: `src/runtime/signal_store.cpp`, `src/runtime/signal_store.h`.

The tuner's **CAN bus** route taps the same raw stream that feeds SignalStore
on the device — useful to confirm an ID is on the wire before chasing a
decoder bug.

## Critical-section invariant

Same rule as ErrorStore (see `error-store.md`): all public mutations and
reads hold a `portMUX_TYPE` for the RMW window. **No LOG / no ALLOC / no
LOCK inside `portENTER_CRITICAL`.** The file header in
`signal_store.cpp` documents the invariant.

The portMUX rather than a FreeRTOS semaphore was chosen so the writes
from taskCAN (high prio, time-sensitive) don't block on priority
inversion when taskUI is mid-frame. The lock is held for tens of
microseconds at most.

## Storage layout

```cpp
struct SignalValue {
    float raw;                // last sample as decoded by CanParser
    float smoothed;           // EMA-filtered value (gauges read this)
    uint32_t lastUpdateMs;    // millis() at last update()
    bool valid;               // false until the first sample arrives
};
SignalValue s_signals[SIGNAL_STORE_MAX_SIGNALS];
```

The table is indexed by `SignalId` (a `uint8_t` enum). Adding a signal
means: bump the enum in `include/signal_map.h`, add the name → id
mapping row in `src/can/signal_map.cpp::kNameToId`, and ensure
`SIGNAL_STORE_MAX_SIGNALS` is large enough (currently 32 — matches
`CONFIG_MAX_SIGNALS`).

## EMA smoothing

`update(id, value)` writes the raw sample AND updates `smoothed` via
`SIGNAL_EMA_ALPHA = 0.2f`:

```cpp
new_smoothed = valid
    ? (SIGNAL_EMA_ALPHA * value + (1.0f - SIGNAL_EMA_ALPHA) * smoothed)
    : value;
```

The first sample seeds `smoothed = value` to avoid a slow climb from 0.
The gauge widget reads `smoothed`; the label widget and warning widget
read `raw` so threshold crossings fire on the actual sample, not the
filtered tail.

## `set(id, value)` — bypass the EMA

`SignalStore::set(id, value)` writes both `raw` and `smoothed` to the
same value without EMA blending. Used by:

- The button widget's optimistic-write path. A toggle button binds to a
  flag signal; on click the button updates the local visual AND writes
  the new value to SignalStore so any other widget watching that signal
  (top bar mode badge) reflects the press
  immediately instead of waiting for the ECU echo (#1285).
- Input bindings that target a signal directly (#833).

The EMA path was wrong for boolean signals — `read()` returned
fractional values like 0.3 after a few EMA steps, and `!= 0.0` was
always true → the toggle got stuck pushing 0.

## `checkTimeouts()` — stale invalidation

`SignalStore::checkTimeouts()` walks the table and clears `valid` for
any entry whose `lastUpdateMs` is older than the configured timeout
(`SIGNAL_DEFAULT_TIMEOUT_MS = 1000` by default; per-signal override in
signals.json). Called from `PageManager::updateWidgets()` every ~100 ms.

Stale invalidation is what drives the gauge widget's placeholder render
(top bar shows the dot as orange/red, gauges show "0"). Without
timeouts, a disconnected sensor would keep displaying the last value
indefinitely.

## `anyValid(ids, count)` — batched lock

The top bar uses status dots that turn green when "any of \{RPM, COOLANT,
BATTERY\}" is valid. Calling `isValid()` three times pays three IRQ-disable
round-trips per UI frame. `anyValid(ids, count)` takes the lock once and
checks all three under a single portMUX (#1342). Used by
`top_bar.cpp::anySignalValid`.

## `getAllValuesSnapshot()` — telemetry-side batched read

Both USB telemetry and BLE telemetry need a consistent snapshot of every
signal value per emit. Calling `read(id)` per signal under the lock is
~30 µs per call; the batched snapshot copies the whole table under one
lock acquire and returns a `const SignalValue[]` for the emitter to walk.
This is what taskUSB and taskBLE both use; it's also what the F1 path in
`PageManager::updateWidgets` uses to drive widget redraws.

## Why this lives in BSS

`s_signals` is a static array, not a `Vec`. The runtime cannot tolerate
heap allocation in the hot RX path (taskCAN at TASK_PRIO_CAN=15, decoding
1000+ frames/sec on a busy bus). The fixed-size table is sized for the
worst case and indexed by integer; lookup is O(1).
