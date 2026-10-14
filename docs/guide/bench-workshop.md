# Bench session: workshop mode

The script for the first workshop-mode session on a Waveshare
ESP32-S3-Touch-LCD-2.8. Work through it top to bottom. Each step lists what to
see and the log lines that prove it. Note anything that differs, with the step
number.

It covers the whole chain: [first flash](first-flash.md),
[workshop mode](settings-panel.md#workshop-mode), the
[workshop socket](../architecture/transports.md#workshop-socket--the-16-mb-boards-wi-fi-link),
a config push, and [firmware OTA](../architecture/firmware-ota.md).

## Before you start

| You need | Why |
| --- | --- |
| The first release that carries workshop mode, `vA`: `canshift-esp32s3-vA-merged.bin` and `-spiffs.bin` | the image under test |
| `canshift-esp32s3-v0.19.0-firmware.bin` and its `.sig`, plus `canshift-esp32s3-vA-firmware.bin.sig` | the OTA tests; v0.19.0 shows a different version on the splash, so the good update is visible |
| A laptop with Chromium or Safari, and an iPhone | the access point, the tuner, the browser console |
| A USB data cable to the laptop, and a serial monitor | the log |
| Optional: CAN wired to an ECU or a simulator | live values over the socket |

**Logs.** Release builds log errors only, so on a release image you only see the
`E` lines below. To see every line, flash a log build of the same tag instead of
the release in step 1:

```bash
git checkout vA
PLATFORMIO_BUILD_FLAGS="-DAPP_LOG_LEVEL=3" pio run -e waveshare_s3_28 -t upload
pio run -e waveshare_s3_28 -t uploadfs
```

Every line is a JSON object such as
`{"log":1,"lvl":"I","tag":"WORKSHOP","msg":"…"}`. Below, only `tag: msg` is
quoted. On the native-USB S3, opening the serial port can reset the board: open
the monitor first, then reset.

## 1. First flash

Follow [First flash](first-flash.md) for the Waveshare: `erase_flash`, then
`write_flash` with the merged image at `0x0` and the SPIFFS image at `0x810000`.

| Observe | Log |
| --- | --- |
| The splash shows the mark and `vA`, then the default dashboard | `BOOT: [BOOT] Ready` |
| Swiping between pages works | — |

## 2. The Workshop mode button

Open Settings by swiping down from the top bar.

| Do | Observe |
| --- | --- |
| Scroll to the bottom of the panel | **WORKSHOP MODE** sits under About |
| If `speed_kph` is fed above 0, close Settings and open it again | the button is gone |
| Feed 0 again (or stop the feed), then reopen | the button is back |

## 3. Enter workshop mode

Tap **WORKSHOP MODE**.

| Observe | Log |
| --- | --- |
| The panel closes, and the card replaces the dash: *Workshop mode / Dash disabled*, network `CANShift-XXXX`, a 12-character password, a QR code, **LEAVE** | `WORKSHOP: Workshop mode requested` |
| | `WORKSHOP: Network credentials created for CANShift-XXXX` (first entry only) |
| No top bar, banner or error bar remains | `UI: Dashboard pages released, overlays hidden` |
| | `WORKSHOP: Workshop mode active — dash disabled` |
| The phone app loses the dash | `HEAP: before BLE release: internal free=… largest=…, psram free=…` |
| | `BLE: BLE stopped for workshop mode — controller memory released` |
| | `HEAP: after BLE release: …` |
| Touching the card anywhere but **LEAVE** does nothing, and swiping changes nothing | — |

Write down the network name and password: step 9 checks that they survive a
reboot.

## 4. The access point

| Do | Observe | Log |
| --- | --- | --- |
| Wait a few seconds | — | `HEAP: workshop before Wi-Fi: …` |
| | | `WORKSHOP: Access point CANShift-XXXX up at 192.168.4.1` |
| | | `WORKSHOP: Answering as http://canshift.local` |
| | | `HEAP: workshop server up: …` |
| Scan the card's QR with the iPhone camera | iOS offers to join `CANShift-XXXX` and joins without typing; it warns *No Internet Connection* and stays joined | — |
| Join the laptop by typing the name and password | it joins | — |

Keep all four `HEAP` lines from steps 3 and 4. They are the workshop-mode
memory budget.

## 5. The tuner over Wi-Fi

| Do | Observe | Log |
| --- | --- | --- |
| Open `http://canshift.local` on the laptop | the tuner loads (or the probe page, if the release pinned no tuner bundle) | — |
| Open `http://192.168.4.1` on the iPhone | the same | — |
| Open devtools → Network on the laptop and reload | every asset `200`, with `Content-Encoding: gzip` and the right `Content-Type`; no CSP errors in the console | — |
| Reload on `/vehicle` | the tuner comes back on that tab | — |
| `curl -sI http://192.168.4.1/nope.js` | `404` | — |
| `curl -sI http://192.168.4.1/` | `Content-Security-Policy: default-src 'self'`, `X-Content-Type-Options: nosniff`, `X-Frame-Options: DENY`, `Referrer-Policy: no-referrer` | — |

## 6. The socket

| Do | Observe | Log |
| --- | --- | --- |
| Let the tuner connect | it shows the dash as connected | `WORKSHOP: Tuner connected on socket N` |
| With CAN live, open the live values | values move about ten times a second | — |
| Open the tuner's log view | the dash's log lines arrive there too | — |
| Open the tuner on the iPhone as well | the laptop's tuner loses the link and the iPhone's connects | `WORKSHOP: Previous tuner socket N replaced` |
| With USB also connected, send `{"cmd":17}` from the serial monitor | the reply shows on USB only, never in the tuner | — |
| Close the iPhone tab | — | `WORKSHOP: Tuner disconnected` |

## 7. A config push

In the tuner on the laptop, change something visible on the dash (a page name
or a widget), then push it.

| Observe | Log |
| --- | --- |
| The burn overlay shows over the card, then clears; the card stays | `USB: PUT_CONFIG_CHUNK: config replaced from N chunks — reloading live` (or `USB: PUT_CONFIG: dashboard.json updated (N bytes) — reloading live`) |
| The dash itself does not change yet: in workshop mode the new config is stored and applies on leave (step 9) | — |

## 8. Firmware updates over Wi-Fi

### 8a. No signature

In the laptop's browser console, on the tuner's page:

```js
const ws = new WebSocket(`ws://${location.host}/ws`)
ws.onmessage = (event) => console.log(event.data)
ws.onopen = () => ws.send(JSON.stringify({ cmd: 48, total: 1296592, sha256: '00'.repeat(32) }))
```

| Observe | Log |
| --- | --- |
| `{"status":"error","message":"ota_unsigned"}` | `OTA: begin refused: ota_unsigned` |
| Nothing on the card changes, and the dash does not reboot | — |

This socket replaces the tuner's. Reload the tuner afterwards.

### 8b. Wrong signature

In the tuner, upload `canshift-esp32s3-v0.19.0-firmware.bin` with
`canshift-esp32s3-vA-firmware.bin.sig`, a valid signature over a different image.

| Observe | Log |
| --- | --- |
| The update overlay shows progress to 100 % | `OTA: begin: target='app1' size=…` |
| The commit fails with `ota_bad_signature`, and the overlay shows the failure | `OTA: signature does not match the release key — aborting` |
| A tap dismisses the overlay back to the card, and the dash does not reboot | — |

Do the good update in step 10, after leaving and re-entering.

## 9. Leave

Tap **LEAVE**.

| Observe | Log |
| --- | --- |
| The dash reboots | `WORKSHOP: Leaving workshop mode — rebooting` |
| | `BOOT: [BOOT] Ready` |
| The dashboard shows the change pushed in step 7 | — |
| The `CANShift-XXXX` network is gone from the phone's list | — |
| The phone app finds the dash over Bluetooth again | — |
| Re-enter workshop mode: the card shows the same network name and password as in step 3, and there is no `Network credentials created` line | `WORKSHOP: Workshop mode active — dash disabled` |

## 10. The good update

Reconnect the tuner (step 5), then upload `canshift-esp32s3-v0.19.0-firmware.bin`
with its own `.sig`. **First move the dash's USB cable from the laptop to a
USB charger.** The reboot must work with no USB host reading, which is the case
that used to hang.

| Observe | Log |
| --- | --- |
| `BEGIN` answers at once, with no multi-second wait before the first chunk | `OTA: begin: target='app1' size=…` |
| Progress to 100 %, then *Update complete* | `OTA: commit: new boot partition='app1' — restart will load it` |
| About 2 s later the dash reboots, with no watchdog panic | — |
| The splash shows `v0.19.0` | — |

To end the session, flash `vA` again with step 1. v0.19.0 has no workshop
mode.

## Report back

For each step, OK or what differed. Add the four `HEAP` lines, and the full
log around anything that failed.
