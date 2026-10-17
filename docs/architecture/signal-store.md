# SignalStore

Source: [`src/runtime/signal_store.cpp`](../../src/runtime/signal_store.cpp),
[`src/runtime/signal_store.h`](../../src/runtime/signal_store.h)

SignalStore is the firmware's shared table of every decoded CAN signal and
every derived value. taskCAN writes it (the CAN parser, the OBD-II poller and
cross-signal expressions), and so do input bindings. taskUI reads it (widgets,
top bar, alerts), as do taskUSB and taskBLE for telemetry.

The tuner's **CAN bus** route taps the same raw stream that feeds SignalStore
on the device — useful to confirm an ID is on the wire before chasing a
decoder bug.

## Critical-section invariant

Same rule as ErrorStore (see `error-store.md`): every public mutation and read
holds a `portMUX_TYPE` for the read-modify-write window. **No LOG, no ALLOC,
no LOCK inside `portENTER_CRITICAL`.**

A portMUX rather than a FreeRTOS semaphore keeps taskCAN (high priority,
time-sensitive) from blocking on priority inversion while taskUI is
mid-frame. The lock is held for microseconds.

## Storage layout

```cpp
struct SignalValue {
    float raw;             // last sample as decoded
    uint32_t lastUpdateMs; // millis() at the last write
    bool valid;            // false until the first sample, and again once stale
    uint32_t timeoutMs;    // stale threshold for this signal
};
SignalValue s_signals[kSignalStoreMaxSignals];
```

The table has `kSignalStoreMaxSignals = 64` slots (`include/app_config.h`),
one per `SignalId`; a `static_assert` in `signal_store.h` keeps it at least
`SignalIds::kSignalCount`. Ids and names come from the Rust `signal-map`
crate (`rust/signal-map/src/lib.rs`); the ids are mirrored as constants in
`src/can/signal_map.h`. See [Signal map](../reference/signals.md).

Non-finite values are dropped at the door: `update()` refuses NaN and
infinity, and logs the first refusal once.

## One value per signal

The store keeps the last decoded sample and nothing else. `update(id, value)`
overwrites it; `read()`, `tryRead()` and the `raw` field of a snapshot all
return it. There is no filtered copy.

**Surface policy: every reader gets the decoded value.**

| Surface                                                                     | Reads |
| --------------------------------------------------------------------------- | ----- |
| Gauge, label, warning, gear, timer and shift-light widgets                  | raw   |
| Alert engine: thresholds, MIL, rev limiter, user critical alerts            | raw   |
| Alert banner value, takeover card value and its RPM context                 | raw   |
| Top-bar metric values and mode flags                                        | raw   |
| Signal stats, cut band, page buttons, control guards, input-binding toggles | raw   |
| Workshop-mode offer gate and motion exit                                    | raw   |
| BLE `TELE` and `ALERT`, USB telemetry                                       | raw   |
| Inputs of cross-signal expressions (`can_parser.cpp`)                       | raw   |

Why one rule:

- **One number per signal on one screen.** The takeover card, the banner,
  the top bar and the widget show the same reading of the same frame.
- **Decisions see the fault when it happens.** A threshold, a flag or a
  guard on a filtered value fires late, filters a short dip and, on a
  per-sample filter, lags by an amount that depends on the frame rate.
  The alert engine already debounces in Rust (hysteresis plus a minimum
  active time), so it needs no second filter in front of it.
- **Derived signals are filtered zero times.** A cross-signal expression
  reads its inputs as decoded, so its output is the expression of the
  current frame, not of a lagged one.

A surface that ever needs a steadier picture filters locally, by time
(`α = 1 − exp(−dt/τ)`), and documents its τ here. Nothing does today.

Input bindings that toggle a signal (`input_buttons.cpp`) write through
`update()` like the CAN parser, so the toggle and an ECU echo read back the
same way.

## `checkTimeouts()` — stale invalidation

`SignalStore::checkTimeouts()` clears `valid` for every entry whose last
write is older than its `timeoutMs`. The default is
`kSignalDefaultTimeoutMs = 500`; a signal's `timeoutMs` in `signals.json`
overrides it, applied through `setTimeout()` when the parser loads the
definitions. `PageManager` calls it about every 100 ms; it returns at once
when nothing is valid.

A stale signal renders as the stale placeholder (`- -` on gauges and the top
bar, `-` on the gear) instead of keeping its last value.

## `anyValid(ids, count)` — batched lock

The top bar's live check asks whether any of RPM, coolant or battery is
valid. `anyValid(ids, count)` answers under one lock instead of one per
id (`top_bar_update.cpp::anySignalValid`).

## `snapshotAll()` — one consistent frame

`PageManager` copies the whole table under one lock acquire every widget
refresh and hands the copy to the alert engine, signal stats, cut band,
widgets and page buttons, so one frame never mixes two states of the
table. Telemetry reads per signal with `tryRead()`.

## Why this lives in BSS

`s_signals` is a static array. The hot RX path (taskCAN at `kTaskPrioCan`,
decoding thousands of frames per second on a busy bus) cannot allocate, and
an id-indexed table makes every lookup O(1).
