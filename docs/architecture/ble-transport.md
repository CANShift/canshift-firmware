# BLE transport

Source: [`src/hal/ble/ble_server.cpp`](../../src/hal/ble/ble_server.cpp)

The BLE stack is the optional secondary transport (USB is primary), compiled
in only when `APP_BLE_ENABLED` is set. Phone-side `canshift-mobile` reads
telemetry and status over GATT notify and writes settings, commands, and
timer ops over GATT write. Sources: `src/hal/ble/ble_server.cpp`,
`src/hal/ble/ble_status.cpp`, `src/hal/ble/ble_telemetry.cpp`,
`src/hal/ble/telemetry_frame.cpp`, `src/hal/ble/ble_timer.cpp`.

## GATT layout

A single primary service (`SVC_UUID` = `4fa0b6a0-…-0001`) with seven
characteristics. Every read/write property is an **encrypted** variant
(`READ_ENC` / `WRITE_ENC`), so the client must be paired before it can read
telemetry or issue a command.

| Characteristic | UUID tail | Properties          | Payload                                                  |
| -------------- | --------- | ------------------- | -------------------------------------------------------- |
| TELE           | `0002`    | READ_ENC, NOTIFY    | binary telemetry frame (see below), ~10 Hz               |
| STATUS         | `0003`    | READ_ENC, NOTIFY    | `{"ver","board_id","can","is_day"}` JSON, ~2 s           |
| SETTINGS       | `0004`    | READ_ENC, WRITE_ENC | read `{"brightness"}`; write `{"brightness","rotation"}` |
| CMD            | `0005`    | WRITE_ENC, WRITE_NR | `{"cmd":"<name>",…}` JSON command                        |
| TIMER_CMD      | `0006`    | WRITE_ENC, WRITE_NR | `{"op":<1–5>}` timer operation                           |
| TIMER_STATE    | `0007`    | READ_ENC, NOTIFY    | `{"st","el","lc","sid","ver"}` JSON                      |
| TIMER_LAP      | `0008`    | NOTIFY              | lap notification                                         |

There is no PASSKEY characteristic — the pairing code is shown on the dash
itself (see below), not exposed over GATT.

## The CMD characteristic is not the USB handler

BLE does **not** reuse the USB command table. `CmdCallbacks::onWrite`
(`ble_server.cpp`) parses its own small, string-keyed command set and routes
each to a runtime action; writes are capped at `BLE_MAX_WRITE_LEN` (256 B):

- `toggle_day_night` / `set_day_night` (`{"day":<bool>}`) → `PendingActions`
- `start_calibration` / `reset_calibration` → `PendingActions`
- `track_state` → `TrackStore::setTelemetry` (lap timer + delta fields)
- `reboot` → `esp_restart()`

Unknown commands are logged and ignored. `TIMER_CMD` is a second, separate
dispatcher (`TimerCmdCallbacks::onWrite`): `{"op":N}` maps `1–5` to
`TimerService::start / pause / resume / reset / lap`. Treat USB as the
complete control surface and BLE as the in-car convenience link.

## Pairing and the security model

`earlyInit()` (and `startStack()`) call `NimBLEDevice::setSecurityAuth(true,
true, true)` (bonding + MITM + secure connections) with
`BLE_HS_IO_DISPLAY_ONLY`. On `onPassKeyRequest` the dash draws a random
6-digit passkey — `esp_random()` with rejection sampling to avoid modulo
bias — and hands it to the UI via `PendingActions::blePasskeyShow` for the
`PasskeyOverlay`. Because the characteristics are `*_ENC`, an unbonded
client cannot read telemetry or write commands.

## Telemetry payload — binary frame

TELE is a packed binary frame, not JSON. `TelemetryFrame::encode`
(`telemetry_frame.cpp`) writes a 3-byte header — `VERSION` (`0x01`) followed
by a little-endian `uint16` presence mask — then one little-endian `int32`
per present field, each value scaled by `SCALE` (×1000). Up to
`MAX_FIELDS` (14) signals are carried; the set and their bit order are
`BLE_TELE_SIGNALS[]` in `ble_telemetry.cpp`. Non-finite values are dropped
(`std::isfinite`). `buildTelemetryPayload` serialises into a stack buffer of
`MAX_FRAME_BYTES`, so the BLE task never touches the heap on the hot path.

This codec mirrors the canonical implementation in `@canshift/core`; the
mobile parser decodes against the same contract, so changing the field set
or order requires updating both sides.

### Dedupe + keepalive

`emitTelemetry` runs a `TelemetryFrame::Deduper`: a frame identical to the
last one is not notified, but a keepalive is forced every `KEEPALIVE_TICKS`
(10 ≈ 1 s) so a subscriber that joins mid-stream still receives a full
frame. The deduper is reset when the subscriber count rises.

## Early init and heap gating

`BleServer::earlyInit()` runs from `boot_sequence.cpp::initBleEarlyIfEnabled`,
**before** `initDisplayHardware()` — NimBLE needs a large contiguous DRAM
block, and the display driver shrinks the largest free block once it comes
up. There is no runtime enable: BLE comes up whenever `APP_BLE_ENABLED` is
compiled in and the heap gates below allow it.

Two heap gates guard the stack:

- `BLE_MIN_HEAP_BYTES` (50 KB) — the contiguous-DRAM floor checked before
  `NimBLEDevice::init`; below it the dash boots BLE-less.
- `BLE_GATT_MIN_HEAP_BYTES` (24 KB) — checked in `startStack()` before
  `setupGatt()` so the GATT phase has its own budget.

## Connection tuning

On connect, `ServerCallbacks::onConnect` calls `updateConnParams` with a
12–24-interval window, a slave latency of 4, and a 400-unit supervision
timeout; the stack advertises with `BLE_PREFERRED_MTU` (247) requested. The
latency lets the phone skip connection events when telemetry is idle without
dropping the link.

## Race against `stop()`

`updateStatus()` and `emitTelemetry()` snapshot the file-scope characteristic
pointer (`s_pStatus`, `s_pTele`) into a local at entry. `BleServer::stop()`
only nulls those pointers on the full-deinit path (when GATT was not
preserved); the advertising-only stop keeps the objects alive, so the
snapshot stays valid even if `stop()` runs on another task mid-call.

## Truncated payload guard

`ArduinoJson` silently truncates when the output buffer is too small, and a
truncated STATUS payload is invalid JSON that would crash the mobile parser.
`updateStatus()` detects truncation (`len == 0 || len >= sizeof(buf)`) and
skips the notify rather than pushing junk over the wire.

## STATUS refresh divider

The ~2 s STATUS cadence is a module-static `s_statusDiv` counter incremented
per `emitTelemetry()` call. At 10 Hz telemetry the divider hits 20 → STATUS
refresh.

## taskBLE stack

`TASK_STACK_BLE = 5120` (`include/app_config.h`) covers the NimBLE callbacks
plus the `buildTelemetryPayload` frame. Bump it only if a future TELE field
set pushes the stack past the fail-safe.
