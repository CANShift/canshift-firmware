# USB transport

Source: [`src/hal/usb/usb_comm.cpp`](../../src/hal/usb/usb_comm.cpp)

USB CDC is the dash's primary host link to canshift-tuner: command/response
JSON lines plus a telemetry line every 200 ms. Sources: `src/hal/usb/usb_comm.cpp`,
`src/hal/usb/usb_dispatch.cpp`, `src/hal/usb/usb_dispatch_files.cpp`,
`src/hal/usb/usb_config_sync.cpp`, `src/hal/usb/usb_envelope.cpp`.

## Wire protocol

- **Link**: `Serial` at 115200 baud. On the CrowPanel that is UART0 behind the
  CH340 bridge; on the native-USB ESP32-S3 it is the USB-serial-JTAG CDC, where
  the baud rate is nominal.
- **Framing**: newline-delimited JSON lines, no other framing.
- **Command shape**: `{"cmd":<int>, …}`. Replies are command-specific JSON;
  errors are always `{"status":"error","message":"<code>"}` (`UsbComm::sendError`).
- **Telemetry**: `{"tele":1,"v":{…signals…}}` every `kTelePeriodTicks` (10) USB
  ticks of `kUsbTaskTickIntervalMs` (20 ms), so every 200 ms, from
  `usb_comm.cpp::sendTelemetry`.
- **Logs**: `{"log":1,"lvl":"…","tag":"…","msg":"…"}` (protocol v2).

`kUsbProtocolVersion = 2` (in `include/app_config.h`) reflects that LOG\_\*
macros emit envelopes instead of plain `[I][TAG]` text.

## Commands

Opcodes are the `CMD_*` constants in
[`usb_comm.h`](../../src/hal/usb/usb_comm.h). `handleCommand()` first offers the
line to the raw-payload handlers (`PUT_CONFIG`, `PUT_FILE`, `OTA_WRITE`), then to
the command table shared with BLE
([`device_commands.cpp`](../../src/runtime/device_commands.cpp)), then to the
USB-only table `kUsbCommands`. Anything else replies `unknown_command`.

| Code   | Name                    | Request                                               | Reply and effect                                                                                    |
| ------ | ----------------------- | ----------------------------------------------------- | --------------------------------------------------------------------------------------------------- |
| `0x01` | `kCmdGetConfig`         | `{"cmd":1}`                                           | `{"status":"ok","config":{…}}`, the on-disk `dashboard.json`                                        |
| `0x02` | `kCmdPutConfig`         | `{"cmd":2,"payload":{…}}`                             | Atomic write of `dashboard.json`, then reboot (see below)                                           |
| `0x03` | `kCmdGetDeviceConfig`   | `{"cmd":3}`                                           | `device.json`, or `config_not_found`                                                                |
| `0x04` | `kCmdPutDeviceConfig`   | `{"cmd":4,"device_config":{…}}`                       | Atomic write of `device.json`, then reboot                                                          |
| `0x05` | `kCmdScreenSettings`    | `{"cmd":5,"brightness":80,"rotation":0}`              | Applies brightness; a `rotation` of 0 or 180 that differs from the current one persists and reboots |
| `0x06` | `kCmdPutFile`           | `{"cmd":6,"path":…,"total":N,"idx":i,"data":"<b64>"}` | Chunked write under `/assets/` or `/fonts/` only; an out-of-order chunk aborts the transfer         |
| `0x07` | `kCmdToggleDayNight`    | `{"cmd":7}`                                           | Flips the theme on the next UI tick                                                                 |
| `0x08` | `kCmdCalibrateTouch`    | `{"cmd":8}`                                           | Runs touch calibration (resistive boards only)                                                      |
| `0x09` | `kCmdSetDayNight`       | `{"cmd":9,"day":true}`                                | Sets the theme; `missing_day` without the bool                                                      |
| `0x0A` | `kCmdResetTouchCal`     | `{"cmd":10}`                                          | Clears the saved calibration                                                                        |
| `0x0B` | `kCmdGetInputBindings`  | `{"cmd":11}`                                          | `input_bindings.json`, or `config_not_found`                                                        |
| `0x0C` | `kCmdPutInputBindings`  | `{"cmd":12,"payload":{…}}`                            | Atomic write of `input_bindings.json`, then reboot                                                  |
| `0x0D` | `CMD_SET_BOARD_PROFILE` | `{"cmd":13,"payload":{…}}`                            | Stores a board id or profile blob in NVS, then reboots ([Board profile](board-profile.md))          |
| `0x0E` | `kCmdPutConfigChunk`    | `{"cmd":14,"total":N,"idx":i,"data":"<b64>"}`         | `dashboard.json` in chunks; the last one commits and reloads the config live, without a reboot      |
| `0x10` | `kCmdGetStatus`         | `{"cmd":16}`                                          | `{"status":"ok","version":…,"protocol":2,"is_day":0,"board_id":…}`                                  |
| `0x11` | `kCmdPing`              | `{"cmd":17}`                                          | `{"status":"ok","uptime_ms":…}`                                                                     |
| `0x20` | `kCmdCanScanStart`      | `{"cmd":32}`                                          | Starts forwarding raw frames                                                                        |
| `0x21` | `kCmdCanScanStop`       | `{"cmd":33}`                                          | Stops forwarding; the ack carries `drops`                                                           |
| `0x22` | `kCmdObdReadDtc`        | `{"cmd":34}`                                          | OBD-II mode 03; replies `{"status":"ok","dtc_bytes":[…]}` when the ECU answers                      |
| `0x23` | `kCmdObdClearDtc`       | `{"cmd":35}`                                          | OBD-II mode 04                                                                                      |
| `0x30` | `kCmdOtaBegin`          | `{"cmd":48,"total":N,"sha256":"<hex>","sig":"<b64>"}` | Opens the inactive app slot (see [Firmware OTA](firmware-ota.md))                                   |
| `0x31` | `kCmdOtaWrite`          | `{"cmd":49,"offset":O,"data":"<b64>"}`                | Writes one chunk; `{"status":"ok","written":N}`                                                     |
| `0x32` | `kCmdOtaEnd`            | `{"cmd":50,"action":"commit"}`                        | `commit` verifies and reboots into the new slot; `abort`, the default, discards                     |
| `0xF0` | `kCmdReboot`            | `{"cmd":240}`                                         | `{"status":"ok","restart":true}`, then restarts                                                     |

## Device-to-host lines

| Line                  | When                               | Shape                                               |
| --------------------- | ---------------------------------- | --------------------------------------------------- |
| Telemetry             | every 200 ms                       | `{"tele":1,"v":{"rpm":3500,…}}`, valid signals only |
| CAN frame (scan mode) | per frame, at most 32 per USB tick | `{"can":1,"id":888,"len":8,"d":[…]}`                |
| CAN health            | every 2 s (`kStatIntervalMs`)      | `{"can_stat":1,"fps":125.0,"errors":0,"missed":0}`  |
| Log                   | on event                           | `{"log":1,"lvl":"info","tag":"USB","msg":"…"}`      |
| Ack                   | once per command                   | `{"status":"ok",…}` or `{"status":"error",…}`       |

`missed` is the TWAI driver's lost-frame total since install: frames dropped because the RX
queue was full plus frames lost to an RX FIFO overrun. The first loss also logs a warning and
pushes `RX_DROPPED` to the error store.

## The serial sink

`serialSink()` (`usb_reply_route.cpp`) writes to UART0: it takes
`Logger::lockUart` with a 50 ms timeout, writes the bytes, appends a newline
if the payload lacks one, then releases the lock. `UsbComm::sendLine()` hands
every line to `routeLine()`.

Without workshop mode, `routeLine()` is the serial sink and nothing else. With
it (`APP_WORKSHOP_ENABLED`), a line is routed by where the command being
handled came from: a reply to a USB line goes to UART0 only, a reply to a
socket line to the socket only, and a line written outside any command
(telemetry, CAN scan, `can_stat`, `heap_stats`) to both. The route is held by
an RAII scope around `handleCommand()` and applies only to the task that set
it. See [Transports](transports.md#workshop-socket--the-16-mb-boards-wi-fi-link).

GET_CONFIG is the one reply too large for a line buffer: the bundled
`dashboard.json` alone is ~28 KB. It asks the route where the reply goes
(`replyReachesSerial()` / `replyReachesSocket()`). On UART0 it streams the
file under the UART lock without buffering it, which keeps a no-PSRAM board
out of a 28 KB allocation. For the socket it reads the file into one
PSRAM-preferred buffer and sends it as a single frame.

On a `Logger::lockUart` timeout the line is dropped rather than interleaved
with a log line.

## RX buffer reservation

`UsbComm::reserveRxBuf()` allocates `USB_RX_BUF_SIZE` through
`Mem::allocPreferSpiram()` — PSRAM first, DRAM fallback. A `static_assert`
pins the buffer to at least `CONFIG_JSON_DOC_DASHBOARD + 256` so a full
dashboard envelope fits. On alloc failure it logs and leaves `s_rxBuf`
NULL; `init()` and `tick()` then early-return, degrading the USB receive
path silently so the rest of the system still boots and the user can
recover over BLE.

## PUT_CONFIG burn flow

`handleCommand()` (`usb_dispatch.cpp`) routes `kCmdPutConfig` to
`handlePutConfig()` (`usb_dispatch_files.cpp`), which writes the dashboard
and **reboots** to apply it:

```
host -> dash:  {"cmd":2,"payload":{<entire dashboard.json>}}
dash:          UsbEnvelope::findPayloadSlice()   // heap-free brace walk → payload byte range
dash:          invokeBurnOverlayShow()            // user feedback, then a short render grace
dash:          xSemaphoreTake(g_lvglMutex)        // busy → showError + sendError("busy")
dash:          StorageDriver::writeFileAtomic("/config/dashboard.json", slice, len)
dash:          (on write failure) give mutex, showError, sendError("write_failed")
dash -> host:  {"status":"ok","rebooting":true}
dash:          Serial.flush(); esp_restart()      // config is picked up on the next boot
```

There is no hot reload: the config takes effect on restart, so the mutex
is not released on the success path — the device reboots while holding it.
Error replies use the shape `{"status":"error","message":"…"}`
(`UsbComm::sendError`).

### Why a raw brace walk

`UsbEnvelope::findPayloadSlice` walks the envelope as raw bytes, honouring
JSON string state + escapes, and returns the byte range of the `"payload"`
value. The slice is handed straight to `StorageDriver::writeFileAtomic` —
no `JsonDocument`, no copy, no pool growth on the ~12 KB envelope. The
`findNeedle` helper is length-bounded because `strstr` short-circuits on
embedded NUL bytes.

### Why hold the LVGL mutex for the burn

`invokeBurnOverlayShow()` paints the overlay once on entry. The storage
write blocks the calling task for the SPIFFS commit (~100 ms); during that
window taskUI must not acquire the mutex, or it could race the burn and
parse a partial config. Holding the mutex makes the burn atomic against
the UI's frame loop.

## CAN scan drain

`drainCanScanQueue()` reads at most 32 frames per `tick()` so telemetry is
not starved on a busy bus. Each frame is serialized as
`{"can":1,"id":<id>,"len":<n>,"d":[b0,…,bn]}`. The queue is allocated
lazily on the first `kCmdCanScanStart`.

## Typed config GET/PUT split

`usb_config_sync.cpp` handles `device.json` and `input_bindings.json`:

- `kCmdGetDeviceConfig` → `sendTypedConfigGet(kConfigPathDevice, "device_config", TypedConfigBody::device)`
- `kCmdGetInputBindings` → `sendTypedConfigGet(kConfigPathInputs, "input_bindings", TypedConfigBody::inputBindings)`
- `kCmdPutDeviceConfig` → `handlePutDeviceConfig`
- `kCmdPutInputBindings` → `handlePutInputBindings`

`sendTypedConfigGet` allocates its response buffer with
`Mem::allocPreferSpiram()` and frees it with `heap_caps_free()` before
returning. The PUT handlers do not stage in one buffer: they stream the
document straight to storage through `ChunkedAtomicWriter`
(`StorageDriver::beginChunkedWriteAtomic` → `appendChunk` → `endChunkedWrite`),
then `sendOkRebooting()` and `esp_restart()` — same reboot-to-apply model
as PUT_CONFIG.

`TypedConfigBody` (`src/config/typed_config_body.h`) holds the on-disk shape
of each file. The PUT handlers, the GET and `ConfigLoader` all go through it:

| File                  | On disk                                     | Wire                     |
| --------------------- | ------------------------------------------- | ------------------------ |
| `device.json`         | the flat object, `{"can_speed_kbps":500,…}` | `{"device_config":{…}}`  |
| `input_bindings.json` | `{"input_bindings":[…]}`                    | `{"input_bindings":[…]}` |

`PUT_DEVICE_CONFIG` writes the object flat. Earlier firmware wrapped it
as `{"device_config":{…}}`, and the loader read nothing from that file. A
device that took such a push still has a wrapped file, so
`TypedConfigBody::device` reads a top-level `device_config` object when there
is one, and the next push rewrites the file flat.

## Firmware OTA

`kCmdOtaBegin`, `kCmdOtaWrite` and `kCmdOtaEnd` are transport-neutral; the
flow, the limits a host must respect and what the dash verifies are on
[Firmware OTA](firmware-ota.md).
