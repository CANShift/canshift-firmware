# Signal map

Source: [`src/can/signal_map.h`](../../src/can/signal_map.h)

A `SignalId` is the integer key into `SignalStore`. Every signal the
firmware understands has a stable id; the on-disk `signals.json` maps frame
bytes onto these ids.

Adding a new signal: add the id and its `NAME_TO_ID` row in the Rust crate
(`rust/signal-map/src/lib.rs`), which resolves names for the firmware
(`signalIdFromName` calls it). Mirror the id as a `SignalIds::k…` constant in
`src/can/signal_map.h`, and add the visual mapping in `canshift-core`. Cargo
tests pin the table on the Rust side and `test_signal_map` pins the ids on the
C++ side.

## Engine

|  id | Name           | Unit | Source                             |
| --: | -------------- | ---- | ---------------------------------- |
|   0 | `rpm`          | RPM  | broadcast or OBD-II mode 01 PID 0C |
|   1 | `throttle_pos` | %    | OBD-II PID 11                      |
|   2 | `map_kpa`      | kPa  | OBD-II PID 0B                      |
|   3 | `boost_bar`    | bar  | derived / broadcast                |
|   4 | `iat_c`        | °C   | OBD-II PID 0F                      |

## Temperatures

|  id | Name             | Unit | Source        |
| --: | ---------------- | ---- | ------------- |
|   5 | `coolant_temp_c` | °C   | OBD-II PID 05 |
|   6 | `oil_temp_c`     | °C   | broadcast     |

## Pressures

|  id | Name             | Unit | Source    |
| --: | ---------------- | ---- | --------- |
|   7 | `oil_press_bar`  | bar  | broadcast |
|   8 | `fuel_press_bar` | bar  | broadcast |

## Fuelling

|  id | Name       | Unit | Source                                  |
| --: | ---------- | ---- | --------------------------------------- |
|   9 | `lambda_1` | λ    | wideband O₂                             |
|  10 | `afr_1`    | AFR  | derived from `lambda_1` × stoichiometry |

## Vehicle

|  id | Name        | Unit     | Source        |
| --: | ----------- | -------- | ------------- |
|  11 | `speed_kph` | km/h     | OBD-II PID 0D |
|  12 | `gear`      | (number) | broadcast     |

## Electrical

|  id | Name            | Unit | Source        |
| --: | --------------- | ---- | ------------- |
|  13 | `battery_volts` | V    | OBD-II PID 42 |

## ECU status flags

|  id | Name                | Source                             |
| --: | ------------------- | ---------------------------------- |
|  20 | `flag_mil`          | MIL bit on the OBD-II status frame |
|  21 | `flag_launch_ctrl`  | broadcast                          |
|  22 | `flag_flat_shift`   | broadcast                          |
|  23 | `flag_anti_lag`     | broadcast                          |
|  24 | `flag_traction_cut` | broadcast                          |

## Map / profile

|  id | Name           | Source    |
| --: | -------------- | --------- |
|  30 | `map_number`   | broadcast |
|  31 | `map_name_idx` | broadcast |

## Lap timer

|  id | Name           | Unit | Source                            |
| --: | -------------- | ---- | --------------------------------- |
|  40 | `lap_timer_ms` | ms   | broadcast or canshift-mobile push |

## Steering-wheel buttons

|  id | Name             | Source    |
| --: | ---------------- | --------- |
|  56 | `wheel_button_1` | broadcast |
|  57 | `wheel_button_2` | broadcast |
|  58 | `wheel_button_3` | broadcast |
|  59 | `wheel_button_4` | broadcast |

Generic slots for buttons on the bus: map any CAN frame, byte and bit onto one
in `signals.json`, then bind a slot in `dashboard.json`'s `pageButtons`. A
pressed button reads non-zero, a released one `0`.

## Numeric gaps

The unused ids (34..39, 41..47, 60..63) are intentional — they reserve room
for future signals in each semantic band without renumbering existing ids.
`SIGNAL_COUNT = 64` is the sentinel for unknown name.

## Signal lifecycle

Every signal goes through `SignalStore`, which holds the latest decoded value,
a last-update timestamp, a valid flag and a stale timeout. Every surface reads
that value as decoded; nothing is smoothed.
The store marks a signal stale once it has gone quiet for longer than its
`timeoutMs` in `signals.json`, 500 ms by default (`kSignalDefaultTimeoutMs`).

Details in [SignalStore](../architecture/signal-store.md).

## Loading signals.json

The loader grades a `signals.json` before it touches the live table:

| Verdict          | Meaning                                                      |
| ---------------- | ------------------------------------------------------------ |
| `Ok`             | valid JSON, a `signals` array, every entry kept              |
| `SignalRejected` | at least one entry the loader drops (bad id, layout, length) |
| `TooMany`        | more entries than the signal cap                             |
| `NoSignalList`   | valid JSON without a `signals` array                         |
| `Unreadable`     | missing, or not valid JSON                                   |

At boot the dash loads `signals.json` when it is `Ok`. Otherwise it reads
`signals.json.bak`, the previous file kept by every atomic write, and takes a
clean backup over a flawed primary. A primary that still yields a table
(`SignalRejected`, `TooMany`) beats a backup that does not. Falling back logs
an error, pushes `SIG_BAK` to the error store and promotes the backup to
`signals.json`. When neither file yields a table, the boot pushes `READ_FAIL`
and CAN parsing stays off.

An upload is stricter: only `Ok` replaces the live file, and an upload without
an `out` block keeps the one already on the device. See
[USB transport](../architecture/usb-transport.md#signalsjson-over-put_file).
