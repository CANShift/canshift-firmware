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
    float display;         // what the dash shows: raw, or eased for slow signals
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

## Decoded and display values

Every slot holds two values. `raw` is the last sample as decoded. `display`
is the number a person reads: equal to `raw` for fast signals, and eased by
a light time-based filter for slow ones.

- `read()` and the `raw` field of a snapshot return the decoded value.
- `readDisplay()`, `tryReadDisplay()` and the `display` field return the
  display value.

### Which signals are eased

The class lives in one table in the catalogue crate,
`rust/signal-map/src/display.rs` (`SMOOTHED_SIGNALS`); every other id is
decoded.

| Class    | Signals                                                                                                              | Display value                        |
| -------- | -------------------------------------------------------------------------------------------------------------------- | ------------------------------------ |
| Smoothed | temperatures (IAT, coolant, oil, EGT, gearbox, diff), oil and fuel pressure, battery volts, fuel level               | eased, τ = `DISPLAY_TAU_MS` = 250 ms |
| Decoded  | RPM, speed, throttle, MAP, boost and boost target, lambda, AFR, gear, knock, flags, buttons, map selection, odometer | the decoded sample                   |

Slow signals carry sensor and quantisation noise around a value that moves
over seconds, so a numeral that flickers in its last digit tells the driver
nothing. Fast signals are the information in their transients: RPM and
speed are read on a ramp, and boost and MAP are read for spool and
overshoot, which a filter hides. Boost stays decoded for that reason even
though it is a pressure.

The filter is `display += α · (raw − display)` with `α = dt / (τ + dt)` and
`dt` the time since the previous sample, so the lag is the same at 10 Hz and
at 50 Hz. The first sample after a stale period seeds `display = raw`.

### Surface policy

| Surface                                                                     | Reads   |
| --------------------------------------------------------------------------- | ------- |
| Gauge and label widgets (the number and the needle)                         | display |
| Top-bar metric values                                                       | display |
| BLE `TELE` and USB telemetry                                                | display |
| Warning, gear, timer and shift-light widgets                                | raw     |
| Alert engine: thresholds, MIL, rev limiter, user critical alerts            | raw     |
| Alert banner value, takeover card value and its RPM context, BLE `ALERT`    | raw     |
| Top-bar mode flags                                                          | raw     |
| Signal stats, cut band, page buttons, control guards, input-binding toggles | raw     |
| Workshop-mode offer gate and motion exit                                    | raw     |
| Inputs of cross-signal expressions (`can_parser.cpp`)                       | raw     |

Why:

- **Decisions read the decoded value.** A threshold, a flag or a guard on a
  filtered value fires late and filters a short dip. The alert engine
  already debounces in Rust (hysteresis plus a minimum active time).
- **An alert shows the value that tripped it.** The takeover card and the
  banner read `raw`, so for a slow signal they can differ from the gauge by
  the filter's lag; that is the intended trade.
- **Telemetry carries what the dash shows,** so the phone and the tuner read
  the same number as the screen.
- **Derived signals are filtered once at most.** A cross-signal expression
  reads its inputs as decoded; its own display value follows its class.

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
table. Telemetry reads per signal with `tryReadDisplay()`.

## Why this lives in BSS

`s_signals` is a static array. The hot RX path (taskCAN at `kTaskPrioCan`,
decoding thousands of frames per second on a busy bus) cannot allocate, and
an id-indexed table makes every lookup O(1).
