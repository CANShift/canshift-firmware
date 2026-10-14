# Transports

The dash talks to the outside world over two transports, and a third on the 16 MB boards in workshop mode. Both carry the same newline-delimited JSON, but they serve different clients and only one of them is always present.

## USB CDC — the primary link

USB is the dash's main host link and it is always available. It is what [canshift-tuner](https://github.com/CANShift/canshift-tuner) speaks to: command/response lines, a telemetry line every 200 ms, and structured log lines. It runs at 115200 baud, through a CH340 UART bridge on the CrowPanel and over native USB on the ESP32-S3, and the protocol version is pinned by `USB_PROTOCOL_VERSION = 2` in `include/app_config.h`.

Incoming lines land in `handleCommand()` (`src/hal/usb/usb_dispatch.cpp`); replies go back out through `UsbComm::sendLine()` (`src/hal/usb/usb_comm.cpp`). The full command surface, framing and burn flow live on [USB CDC](../architecture/usb-transport.md).

## BLE GATT — the optional link

BLE is a secondary transport for the mobile companion, and it only exists when the firmware is built with `APP_BLE_ENABLED` (every BLE call site in `src/main.cpp` sits behind that guard). It exposes a single NimBLE GATT service: telemetry and status go out over notify, and a small, fixed command set (day/night, calibration, reboot, timer) comes in over a write characteristic handled in `src/hal/ble/ble_server.cpp`. The GATT layout and pairing flow are on [BLE GATT](../architecture/ble-transport.md).

> [!NOTE]
> BLE and its NimBLE stack reserve their heap arena early in `main()`, before the display comes up — a no-PSRAM WROOM cannot claim a large contiguous block once LVGL owns its pool. That ordering is the subject of [Boot sequence](../architecture/boot-sequence.md).

## Workshop socket — the 16 MB boards' Wi-Fi link

Only builds with `APP_WORKSHOP_ENABLED` (`waveshare_s3_28`) carry it, and only
[workshop mode](../guide/settings-panel.md#workshop-mode) starts it. Entering
releases BLE first. `taskBLE` then spawns a one-shot task that raises a WPA2
access point with the stored credentials (`192.168.4.1`, also answering as
`canshift.local` over mDNS) and an `esp_http_server` on port 80. Nothing runs
on the radio outside workshop mode, and leaving reboots.

| Route | Serves |
| --- | --- |
| `GET /ws` | WebSocket into the USB command handler |
| `GET /*` | the tuner from SPIFFS `/tuner/` |

**The socket is the USB protocol, framed differently.** Each text frame
carries exactly one JSON line in either direction, with no trailing newline.
Incoming lines are queued to `taskUSB`, which runs them through the same
`handleCommand()` as a USB line. A reply goes back on the transport its command
came from and only there. Unsolicited lines (`tele`, `can`, `can_stat`,
`heap_stats`) go to both. That routing lives in `usb_reply_route.cpp`. Log lines
(`{"log":1,…}`) are mirrored to the socket too, because over Wi-Fi it is the
only link: `Logger::emit` hands each line to a hook that copies it into a
16-line queue, and `taskUSB` sends it. A full queue drops the line and reports
the count later. Every socket write happens on `taskUSB`, so frames never
interleave.

| Rule | Value |
| --- | --- |
| Clients | one; a new connection replaces the previous one |
| Frame | text, final, under `USB_RX_BUF_SIZE` (16 KB + 256); anything else closes the socket |
| Queue | two lines; a third waits up to 2 s, then the socket closes |
| Send timeout | 1 s per frame, so a stalled phone cannot hold `taskUSB` longer |

**Static files.** A request for `/x` serves `/tuner/x.gz` with
`Content-Encoding: gzip` when it exists, else `/tuner/x`. `/` is
`index.html`. A missing path without an extension falls back to `index.html`
so the tuner's client-side routes load; a missing file with an extension is a
404. Every response carries `Content-Security-Policy: default-src 'self'`,
`X-Content-Type-Options: nosniff`, `X-Frame-Options: DENY`,
`Referrer-Policy: no-referrer` and `Cache-Control: no-cache`.

SPIFFS caps a full path at 31 characters, so the bundle's paths, including
`/tuner/` and `.gz`, must fit: 21 characters for the name inside `/tuner/`.
Longer names and anything with `..`, `%`, `\` or a hidden segment are refused
(`rust/workshop-mode/src/tuner_assets.rs`). The CSP also rules out inline
scripts and styles in the bundle.

`scripts/stage_fs_data.py` stages `data/` into `.pio/build/<env>/spiffs_data/`
for `buildfs` and `uploadfs`. On every env without `APP_WORKSHOP_ENABLED` it
leaves `tuner/` out, so the 4 MB boards' 832 KB image never carries the bundle.
On the workshop envs it fills `tuner/` from the first of:

| Source | When |
| --- | --- |
| `../canshift-tuner/dist-tuner/` | a sibling checkout has run `npm run bundle:dash` |
| the release pinned in `tuner-version.txt` | `tag` is a `vX.Y.Z` |
| `data/tuner/` as committed | nothing is pinned (`tag=none`) |

The pinned release is
`canshift-tuner-dash-<tag>.tar.gz` from CANShift/canshift-tuner's Releases,
cached under `.pio/tuner-cache/`. Its sha256 must match the pin or the build
fails. Its entries must be flat files whose names fit the SPIFFS path above,
and it must hold `index.html.gz`. `python3 scripts/pin_tuner.py vX.Y.Z` writes
the pin from the release's `.sha256` asset. Like the core schema pin, a sibling
checkout wins so local work never needs a release; release builds run in CI,
where there is none.

The committed `data/tuner/` is a probe page that opens the socket and sends
`PING` and `GET_STATUS`, so a board can be bench-tested before any tuner
release exists.

## What they share

Both transports move JSON, so telemetry and status use the same shapes on either link. What they do **not** share is the command handler: USB routes lines through the full dispatcher, while BLE's write characteristic services only its own short command list. Treat USB as the complete control surface and BLE as the in-car convenience link.
