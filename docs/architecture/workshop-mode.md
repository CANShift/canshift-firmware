# Workshop mode

> [!NOTE]
> **Planned, not built yet.** This page is the design the work is built
> against. Each delivery slice below links its issue once filed.

Workshop mode turns the dash into its own configurator. One button in the
Settings panel stops the dashboard, switches the radio from Bluetooth to a
Wi-Fi access point, and serves the tuner from flash. Any browser on any
phone or laptop opens it. No Chrome, no USB cable, no install.

## Why

The tuner used to talk to the dash over Web Serial. That meant a desktop
Chromium browser, a cable, and a serial link that resets the board on DTR and
drops bytes on the native-USB S3. None of that works from an iPhone, which is
what a driver has in the garage.

The 16 MB boards have the room to carry the tuner themselves: the `spiffs`
partition in `ota_16mb.csv` is 7.5 MB, and the gzipped tuner is about 0.5 MB.

## The three ways to touch a dash

| Moment | Tool | Link |
| --- | --- | --- |
| First flash of a blank board | `esptool` from a terminal, documented step by step | USB, once |
| Configuration and firmware updates | The tuner, served by the dash in workshop mode | Wi-Fi access point |
| Driving | The mobile app, unchanged | BLE |

After the first flash, the USB cable is only for recovery.

## Entering and leaving

```mermaid
stateDiagram-v2
    Driving --> Workshop: Settings → Workshop mode (stationary only)
    Workshop --> Driving: Leave workshop → reboot
```

- **The button sits in the Settings panel** and is only offered while the car
  is stationary (`speed_kph` at 0, or no speed signal mapped).
- **Leaving is a reboot.** Freeing the Bluetooth controller's memory cannot be
  undone without a restart, and a reboot returns every subsystem to a known
  state. No partial teardown to get wrong.
- **The screen locks to a workshop card**: "Workshop mode, dash disabled", the
  network name, its password, and a QR code that joins the network in one scan.
  Touch does nothing on that card except the Leave button.

## What runs in workshop mode

| Subsystem | State | Reason |
| --- | --- | --- |
| BLE | Stopped, controller memory released | Wi-Fi and BLE share one radio; the RAM goes to the web server |
| Dashboard pages | Released, only the workshop card is drawn | The LVGL page trees are the largest heap user after BLE |
| CAN decode | Running | The tuner shows live values while you map signals |
| USB | Running | Recovery path stays open |
| Wi-Fi | Access point, WPA2 | — |
| HTTP + WebSocket server | Running | Serves the tuner and carries its commands |

The access point is never open. Its password is generated per board on first
entry, stored in NVS and shown on the workshop card, so a stranger in the same
car park cannot reach the dash.

## One protocol

The tuner's WebSocket carries the same newline-delimited JSON lines as USB.
They go into the same `handleCommand()` dispatcher
([USB transport](usb-transport.md)), so config push and pull, CAN scan, and
firmware OTA behave the same on both links. The wire contract stays in
`@canshift/core`. Workshop mode adds a transport, not a protocol.

## Offline by construction

A phone on the dash's access point has no internet. Everything the tuner needs
ships in flash: the app bundle, its fonts, and the ECU catalogue. Nothing in the
tuner may fetch from the network. This is also why analytics and the contact
form are gone from it.

A firmware update means downloading the signed `.bin` from
[Releases](https://github.com/CANShift/canshift-firmware/releases) first, then
uploading it with its `.sig` from the tuner. The dash verifies the Ed25519
signature against the release key compiled into it, and switches to the new slot
only when it verifies ([Firmware OTA](firmware-ota.md),
[Firmware signing](../reference/firmware-signing.md)).

## The tuner, reduced

The dash canvas keeps full fidelity against the
[dash design system](../design/DASH_DESIGN_SYSTEM.md); it is the reason the
tuner exists. Everything around it is plain forms, as simple as possible.

| Tab | Holds |
| --- | --- |
| **Dash** | Page strip, canvas, widget properties |
| **Vehicle** | ECU profile, signals, live values, CAN bus settings |
| **Device** | Units, brightness, board, firmware upload, config backup and restore |

Removed: Welcome, Home, Contact, analytics, and the Web Serial flasher.

## Where the code lives

The tuner stays in its own repo,
[canshift-tuner](https://github.com/CANShift/canshift-tuner). It is a Node and
Vite project with its own CI and an npm dependency on `@canshift/core`. Mixing
that toolchain into PlatformIO would slow both pipelines and blur ownership.
The coupling is one artifact: each tuner release publishes a gzipped bundle,
and the firmware build pins a tuner version and copies that bundle into
`data/tuner/` before `uploadfs`. This works like the schema pin, which is
already the firmware's only link to core.

## Boards

Workshop mode needs the 16 MB layout, so today that means
`waveshare_s3_28` only. The 4 MB boards keep USB configuration and get no
workshop button. That is a deliberate limit, not an oversight.

## Delivery slices

Each slice ships and is verified on its own, in this order:

1. **Firmware: workshop mode.** Settings button, BLE stop, workshop card with
   QR, leave by reboot.
2. **Firmware: access point and server.** WPA2 access point, static gzipped
   files from `data/tuner/`, WebSocket into `handleCommand()`.
3. **Tuner: reduction.** Remove Welcome, Home, Contact and analytics; three
   tabs; offline assets.
4. **Tuner: WebSocket transport.** Replaces Web Serial; release publishes the
   bundle.
5. **Firmware: tuner pin.** The build fetches the pinned bundle into
   `data/tuner/`.
6. **Firmware: upload OTA over Wi-Fi.** Signed `.bin` upload from the Device
   tab.
7. **Docs: first flash from a terminal.** `esptool` steps move here from the
   tuner's manual-flash page, and the tuner's install docs go.

## Mobile

The mobile app is unchanged. Pushing a dashboard update from the app over BLE
remains possible later, but it is not part of this work. Today BLE carries only
a fixed command set ([BLE transport](ble-transport.md)).
