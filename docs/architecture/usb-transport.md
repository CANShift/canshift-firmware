# USB transport

Source: [`src/hal/usb/usb_comm.cpp`](../../src/hal/usb/usb_comm.cpp)

USB CDC is the dash's primary host link to canshift-tuner: command/response
JSON lines + ~10 Hz telemetry. Sources: `src/hal/usb/usb_comm.cpp`,
`src/hal/usb/usb_dispatch.cpp`, `src/hal/usb/usb_dispatch_files.cpp`,
`src/hal/usb/usb_config_sync.cpp`, `src/hal/usb/usb_envelope.cpp`.

## Wire protocol

- **Baud**: 115200 over the CH340 UART0 bridge.
- **Framing**: newline-delimited JSON lines, no other framing.
- **Command shape**: `{"cmd":<int>, …}`. Replies are command-specific JSON.
- **Telemetry**: `{"tele":1,"v":{…signals…}}` emitted every `TELE_PERIOD_TICKS`
  (10 ticks ≈ 10 Hz) from `usb_comm.cpp::sendTelemetry`.
- **Logs**: `{"log":1,"lvl":"…","tag":"…","msg":"…"}` (protocol v2).

`USB_PROTOCOL_VERSION = 2` (in `include/app_config.h`) reflects that LOG\_\*
macros emit envelopes instead of plain `[I][TAG]` text.

## The serial sink

The send path is a single sink. `serialSink()` (`usb_comm.cpp`) writes to
UART0: it takes `Logger::lockUart` with a 50 ms timeout, writes the bytes,
appends a newline if the payload lacks one, then releases the lock.
`UsbComm::sendLine()` calls `serialSink()` directly — there is no aux sink and
no per-task dispatch sink. UART0 is the only place a reply is written.

On a `Logger::lockUart` timeout the write still goes through, unprotected:
the sink favours "degrade, don't drop", so a busy logger never loses a
command ack — at worst a log line and a reply interleave on the wire.

## RX buffer reservation

`UsbComm::reserveRxBuf()` allocates `USB_RX_BUF_SIZE` through
`Mem::allocPreferSpiram()` — PSRAM first, DRAM fallback. A `static_assert`
pins the buffer to at least `CONFIG_JSON_DOC_DASHBOARD + 256` so a full
dashboard envelope fits. On alloc failure it logs and leaves `s_rxBuf`
NULL; `init()` and `tick()` then early-return, degrading the USB receive
path silently so the rest of the system still boots and the user can
recover over BLE.

## PUT_CONFIG burn flow

`handleCommand()` (`usb_dispatch.cpp`) routes `CMD_PUT_CONFIG` to
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
lazily on the first `CMD_CAN_SCAN_START`.

## Typed config GET/PUT split

`usb_config_sync.cpp` handles `device.json` and `input_bindings.json`:

- `CMD_GET_DEVICE_CONFIG` → `sendTypedConfigGet(CONFIG_PATH_DEVICE, "device_config", nullptr)`
- `CMD_GET_INPUT_BINDINGS` → `sendTypedConfigGet(CONFIG_PATH_INPUTS, "input_bindings", "input_bindings")`
- `CMD_PUT_DEVICE_CONFIG` → `handlePutDeviceConfig`
- `CMD_PUT_INPUT_BINDINGS` → `handlePutInputBindings`

`sendTypedConfigGet` allocates its response buffer with
`Mem::allocPreferSpiram()` and frees it with `heap_caps_free()` before
returning. The PUT handlers do not stage in one buffer: they stream the
document straight to storage through `ChunkedAtomicWriter`
(`StorageDriver::beginChunkedWriteAtomic` → `appendChunk` → `endChunkedWrite`),
then `sendOkRebooting()` and `esp_restart()` — same reboot-to-apply model
as PUT_CONFIG.

The `unwrapKey` argument handles the disk-vs-wire shape difference:
`input_bindings.json` wraps its array under `{"input_bindings":[…]}` on
disk, so `sendTypedConfigGet` lifts the body out under that key; `device.json`
is already flat and passes `unwrapKey = nullptr`.
