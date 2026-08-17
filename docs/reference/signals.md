# Signal map

Source: [`src/can/signal_map.h`](../../src/can/signal_map.h)

The `SignalId` enum is the integer key into `SignalStore`. Every signal the
firmware understands has a stable id; the on-disk `signals.json` maps frame
bytes onto these ids.

Adding a new signal: bump the enum in
`canshift-firmware/include/signal_map.h`, add the name → id row in
`canshift-firmware/src/can/signal_map.cpp::kNameToId`, mirror the entry in the
Rust crate (`rust/signal-map/src/lib.rs`), and add the visual mapping in
`canshift-core`. Source of truth across both languages — Unity tests pin the
ids; cargo tests pin them on the Rust side.

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

## Numeric gaps

The gaps (15..19, 25..29, 32..39, 41..63) are intentional — they reserve room
for future signals in each semantic band without renumbering existing ids.
`SIGNAL_COUNT = 64` is the sentinel for unknown name.

## Signal lifecycle

Every signal goes through `SignalStore`, which holds the latest raw value, an
EMA-smoothed value, a last-update timestamp, and a valid flag. The store
invalidates entries older than `SIGNAL_DEFAULT_TIMEOUT_MS = 1000` (default;
per-signal override in `signals.json`).

Details in [SignalStore](../architecture/signal-store.md).
