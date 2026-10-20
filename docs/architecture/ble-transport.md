# BLE transport

Source: [`src/hal/ble/ble_server.cpp`](../../src/hal/ble/ble_server.cpp)

The BLE stack is the optional secondary transport (USB is primary), compiled
in only when `APP_BLE_ENABLED` is set. Phone-side `canshift-mobile` reads
telemetry and status over GATT notify and writes settings, commands, and
timer ops over GATT write. Sources: `src/hal/ble/ble_server.cpp`,
`src/hal/ble/ble_status.cpp`, `src/hal/ble/ble_telemetry.cpp`,
`src/hal/ble/telemetry_frame.cpp`, `src/hal/ble/ble_alert.cpp`,
`src/hal/ble/alert_frame.cpp`, `src/hal/ble/ble_timer.cpp`.

## GATT layout

A single primary service (`kSvcUuid` = `4fa0b6a0-…-0001`) with eight
characteristics. Every read/write property is an **authenticated** variant
(`READ_AUTHEN` / `WRITE_AUTHEN`), so the client must hold a passkey-authenticated
bond before it can read telemetry, receive a notification or issue a command.
An encrypted-but-unauthenticated link (Just Works) is refused at the ATT layer.

| Characteristic | UUID tail | Properties                | Payload                                                  |
| -------------- | --------- | ------------------------- | -------------------------------------------------------- |
| TELE           | `0002`    | READ_AUTHEN, NOTIFY       | binary telemetry frame (see below), ~10 Hz               |
| STATUS         | `0003`    | READ_AUTHEN, NOTIFY       | `{"ver","board_id","can","is_day"}` JSON, ~2 s           |
| SETTINGS       | `0004`    | READ_AUTHEN, WRITE_AUTHEN | read `{"brightness"}`; write `{"brightness","rotation"}` |
| CMD            | `0005`    | WRITE_AUTHEN, WRITE_NR    | `{"cmd":"<name>",…}` JSON command                        |
| TIMER_CMD      | `0006`    | WRITE_AUTHEN, WRITE_NR    | `{"op":<1–5>}` timer operation                           |
| TIMER_STATE    | `0007`    | READ_AUTHEN, NOTIFY       | `{"st","el","lc","sid","ver"}` JSON                      |
| TIMER_LAP      | `0008`    | READ_AUTHEN, NOTIFY       | lap notification                                         |
| ALERT          | `0009`    | READ_AUTHEN, NOTIFY       | binary critical-alert frame (see below), on change + 1 s |

There is no PASSKEY characteristic — the pairing code is shown on the dash
itself (see below), not exposed over GATT.

## The CMD characteristic is not the USB handler

BLE does **not** reuse the USB command table. `CmdCallbacks::onWrite`
(`ble_server.cpp`) parses its own small, string-keyed command set and routes
each to a runtime action; writes are capped at `kBleMaxWriteLen` (256 B):

- `toggle_day_night` / `set_day_night` (`{"day":<bool>}`) → `PendingActions`
- `start_calibration` / `reset_calibration` → `PendingActions`
- `track_state` → `TrackStore::setTelemetry` (lap timer + delta fields)
- `reboot` → `esp_restart()`
- `ack_alert` → acknowledges the critical alert on screen, or the one named by
  an optional `"source"` (the ALERT source id)

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
`PasskeyOverlay`. Because the characteristics are `*_AUTHEN`, only a client
that entered the passkey can read telemetry, subscribe or write commands.

The IO capability pair decides whether a pairing is authenticated: a
`KeyboardDisplay` initiator (the iPhone) against the dash's `DisplayOnly`
yields passkey entry, while a `NoInputNoOutput`, `DisplayOnly` or
`DisplayYesNo` initiator resolves to Just Works, which NimBLE completes with an
encrypted but unauthenticated link and no passkey card. The firmware does not
rely on the ATT flags alone to keep such a peer out; the admission policy lives
in `rust/ble-link` (`ble_link_rs.h`) and `src/hal/ble/ble_link_guard.cpp`
applies it:

| Moment                     | Rule                                                                                                                                   |
| -------------------------- | -------------------------------------------------------------------------------------------------------------------------------------- |
| `onAuthenticationComplete` | a link that is not both encrypted and authenticated is dropped; if it bonded, the bond is deleted first so it cannot evict the phone's |
| `onSubscribe`              | a subscriber whose link is encrypted without authentication is dropped the same way; an unencrypted subscriber is left to pair         |
| `BleServer::tick`          | nothing is notified until the link is admitted; a peer still unauthenticated `PAIRING_DEADLINE_MS` (20 s) after connecting is dropped  |
| `pushStatusNotify`         | gated on the same admission check                                                                                                      |

Dropping the peer restarts advertising through `onDisconnect`, so a rogue or
idle central cannot hold the single connection slot (`CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1`).

The passkey card is never drawn over an active critical alert: `main.cpp`
drops the request while `AlertTakeover::isActive()`, and the takeover root is
raised to the foreground each time it shows, so a card already on screen goes
behind it. The phone's pairing attempt then times out and is retried once the
alert clears.

## Telemetry payload — binary frame

TELE is a packed binary frame, not JSON. `TelemetryFrame::encode`
(`telemetry_frame.cpp`) writes a 3-byte header — `kVersion` (`0x01`) followed
by a little-endian `uint16` presence mask — then one little-endian `int32`
per present field, each value scaled by `kScale` (×1000). Up to
`kMaxFields` (14) signals are carried; the set and their bit order are
`kBleTeleSignals[]` in `ble_telemetry.cpp`. Non-finite values are dropped
(`std::isfinite`). `buildTelemetryPayload` serialises into a stack buffer of
`kMaxFrameBytes`, so the BLE task never touches the heap on the hot path.

This codec mirrors the canonical implementation in `@canshift/core`; the
mobile parser decodes against the same contract, so changing the field set
or order requires updating both sides.

### Dedupe + keepalive

`emitTelemetry` runs a `TelemetryFrame::Deduper`: a frame identical to the
last one is not notified, but a keepalive is forced every `kKeepaliveTicks`
(10 ≈ 1 s) so a subscriber that joins mid-stream still receives a full
frame. The deduper is reset when the subscriber count rises.

## Alert payload — binary frame

ALERT carries the takeover state so the phone mirrors the dash instead of
judging thresholds itself. The format is canonical in `@canshift/core`
(`docs/ble-alert-frame.md`, `encodeAlertFrame` / `decodeAlertFrame`);
`AlertFrame::encode` (`alert_frame.cpp`) mirrors it and pins the same byte
vectors in `test/test_alert_frame/`.

Every `AlertTakeover::update` (100 ms, UI task) hands the critical and
acknowledged source masks and the source on screen to
`AlertTakeoverBroadcast::publish`, which lists each critical source — reading,
crossed limit, acknowledged and on-screen flags, seconds since it turned
critical, signal key — with the on-screen one first, and stores the encoded
bytes in `AlertBroadcast` behind a spinlock. The store bumps a generation only
when the bytes change. The BLE task copies the latest frame each tick and
notifies on a new generation, or every `kKeepaliveTicks` as a keepalive; a new
subscriber forces a send. Whole entries that do not fit the 244-byte notify
payload are dropped, never the on-screen one. With no critical source the frame
is `02 00`.

The source id is the takeover's source index: `AlertSources::kSources` in
order, then one id per `criticalAlerts` entry in list order, so the phone can
name the entry that fired. An entry the dash cannot arm keeps its id and never
turns critical. core's parity test checks the built-in order against its
`ALERT_SOURCES` and the id count against `ALERT_SOURCE_ID_COUNT`.

## Early init and heap gating

`BleServer::earlyInit()` runs from `boot_sequence.cpp::initBleEarlyIfEnabled`,
**before** `initDisplayHardware()` — NimBLE needs a large contiguous DRAM
block, and the display driver shrinks the largest free block once it comes
up. There is no runtime enable: BLE comes up whenever `APP_BLE_ENABLED` is
compiled in and the heap gates below allow it.

Two heap gates guard the stack:

- `kBleMinHeapBytes` (50 KB) — the contiguous-DRAM floor checked before
  `NimBLEDevice::init`; below it the dash boots BLE-less.
- `kBleGattMinHeapBytes` (24 KB) — checked in `startStack()` before
  `setupGatt()` so the GATT phase has its own budget.

When the stack fails to come up, `requestStop()` sets a flag so taskBLE tears
it down and returns the heap. Nothing asks for a restart.

## Connection tuning

On connect, `ServerCallbacks::onConnect` calls `updateConnParams` with a
12–24-interval window, a slave latency of 4, and a 400-unit supervision
timeout; the stack advertises with `kBlePreferredMtu` (247) requested. The
latency lets the phone skip connection events when telemetry is idle without
dropping the link.

## Advertising cadence and TX power

Advertising starts fast — every 30–60 ms — on boot, after a disconnect and when
BLE is restarted, so a phone that is looking finds the dash at once. After 30 s
with no connection (`advertising_backs_off` in `rust/ble-link`) it restarts at
417.5–437.5 ms, one of the intervals Apple's accessory guidelines list, which
cuts the advertising events about tenfold while a foreground app still connects
within a second. The next disconnect returns to the fast cadence.

| Radio use   | TX power | Why                                                                                                                |
| ----------- | -------- | ------------------------------------------------------------------------------------------------------------------ |
| Advertising | +3 dBm   | Discovery only has to reach the cabin; at 2 m through a body the phone still sees about −75 dBm                    |
| Connection  | +9 dBm   | Telemetry streams for the whole drive; the margin keeps the link up when the phone is in a pocket or behind a seat |

Both are set through `esp_ble_tx_power_set` (default type for the link, the
advertising type for advertising), and a refused level is logged and pushed to
the ErrorStore.

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
