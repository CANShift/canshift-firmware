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
    float smoothed;        // EMA of the samples
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

Non-finite values are dropped at the door: `update()` and `set()` refuse
NaN and infinity, and log the first refusal once.

## Raw and smoothed

`update(id, value)` stores the sample in `raw` and folds it into `smoothed`
with `kSignalEmaAlpha = 0.2f`:

```cpp
smoothed = valid ? (kSignalEmaAlpha * value + (1.0f - kSignalEmaAlpha) * smoothed)
                 : value;
```

The first sample after a stale period seeds `smoothed = value`, so it does not
climb from 0. The EMA advances once per sample, so how far it lags depends
on the signal's frame rate.

`read()` and `tryRead()` return `smoothed`; `readRaw()` and the `raw` field
of a snapshot return the sample. `smoothed` decays as 0.8ⁿ and never
returns to exactly 0, so a `!= 0` test on it keeps a dropped flag lit and a
threshold on it delays a real fault and filters a short one.

Which value each surface reads today:

| Reads `raw`                                                               | Reads `smoothed`                                               |
| ------------------------------------------------------------------------- | -------------------------------------------------------------- |
| Gauge, label, warning, gear and timer widgets (`widget_factory.cpp`)      | Shift-light strip                                              |
| Alert engine thresholds: coolant, oil, battery, MIL, user critical alerts | Rev-limiter level and its blink (the alert engine's RPM input) |
| Signal stats, cut band, page buttons                                      | Top-bar metric values                                          |
| Top-bar mode flags, input-binding toggles                                 | Alert banner value; takeover card value and its RPM context    |
| Workshop-mode offer gate                                                  | Workshop-mode motion exit                                      |
|                                                                           | BLE `TELE` and `ALERT`, USB telemetry                          |
|                                                                           | Inputs of cross-signal expressions (`can_parser.cpp`)          |

A derived signal therefore smooths an already smoothed input once more when
it is written through `update()`.

## `set(id, value)` — bypass the EMA

`SignalStore::set(id, value)` writes `raw` and `smoothed` to the same value.
Input bindings that toggle a signal use it (`input_buttons.cpp`), so every
reader sees the new state at once instead of waiting for the ECU echo. Flag
readers use `readRaw()`, so a toggle written by `set()` and a flag echoed by
the ECU through `update()` both read back exactly.

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
