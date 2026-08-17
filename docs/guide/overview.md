# What is CANShift?

CANShift is a small ESP32 touch screen wired to your car's CAN bus. It listens for the frames your ECU broadcasts — RPM, coolant temp, oil pressure, lambda, gear, vehicle speed, you name it — decodes them in real time, and paints them on configurable dashboard pages you design from a browser. No proprietary tooling, no cloud round-trip.

## What you'll have at the end

- A **dash** on the steering column or centre console, reading signals at 1 kHz from the bus and rendering them at ~60 fps.
- A **tuner** open in a Chromium tab on a laptop — drag-and-drop pages, ECU profile picker, live preview, CAN scan.
- A **mobile companion** (optional) that mirrors telemetry over BLE when the laptop isn't in the car.

The dash boots in ~2 seconds, holds a 2-second splash so you can read the version, then drops you onto the page you marked as default. Swipe sideways for other pages, swipe down for Settings, swipe up for the diag drawer.

## What runs where

<pre class="cs-ascii">{`
  ┌───────────┐   USB-C / Web Serial     ┌──────────────────┐
  │  Tuner    │ ◄──────────────────────► │      Dash        │
  │  browser  │   JSON @ 115200          │   ESP32 + LVGL   │
  └───────────┘                          └────────┬─────────┘
                                                  │ CAN @ 500 kbit/s
                                                  ▼
                                          ┌──────────────────┐
                                          │      Your ECU    │
                                          └──────────────────┘
                                                  ▲
  ┌───────────┐   BLE  (optional)                 │
  │  Mobile   │ ◄────────────────────────────────┘
  │  Expo app │   telemetry mirror
  └───────────┘
`}</pre>

The dash is the only thing on the car. The tuner is just a browser tab — open it when you want to change a page, then close it. The mobile app pairs over BLE for in-car telemetry without the laptop.

## What you'll need

<div class="cs-spec-sheet">
  <div class="title">Bill of materials — reference build</div>
  <dl>
    <dt>Screen</dt>
    <dd>Elecrow CrowPanel 2.8″ ESP32 (ILI9341 SPI + XPT2046 touch)</dd>
    <dt>CAN transceiver</dt>
    <dd>NXP TJA1051T/3 breakout, 3.3 V tolerant</dd>
    <dt>Wiring</dt>
    <dd>
      Dupont jumpers between screen ⇄ transceiver, OBD-II or direct splice to your ECU CAN H/L
    </dd>
    <dt>Cable</dt>
    <dd>USB-C data cable (not power-only) for flashing</dd>
    <dt>Browser</dt>
    <dd>Chromium-based (Chrome, Edge, Brave, Arc, Opera) — Firefox has no Web Serial</dd>
  </dl>
</div>

## How long this takes

| Step                                                                                                                  | Reading time |               Doing time |
| --------------------------------------------------------------------------------------------------------------------- | -----------: | -----------------------: |
| [Wire up + first flash](https://github.com/CANShift/canshift-tuner/blob/main/docs/install/first-flash.md)             |        4 min | ~5 min for a fresh board |
| [Pick an ECU profile, edit a page](https://github.com/CANShift/canshift-tuner/blob/main/docs/configure/with-tuner.md) |        3 min |   ~10 min the first time |
| [Use the dash on the road](../guide/navigating-pages.md)                                                              |        2 min |                        0 |

If something goes wrong, the boot logs are explicit — see [Boot diagnostics](https://github.com/CANShift/canshift-tuner/blob/main/docs/install/boot-issues.md) for the failure dictionary.

## Next

Read [First flash](https://github.com/CANShift/canshift-tuner/blob/main/docs/install/first-flash.md) — it walks the box-to-splash path in five steps, with a serial monitor on standby in case the boot logs need a second look.
