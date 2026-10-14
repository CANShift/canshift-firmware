# First flash

A blank board gets its firmware once, from a terminal, with `esptool`. After
that the dash is updated without a cable: in [workshop mode](../architecture/workshop-mode.md)
on a 16 MB board, or over USB from the tuner on a 4 MB board. Come back to
this page only to recover a board that no longer boots.

> [!NOTE]
> The tuner's in-browser Web Serial flasher is being removed
> ([canshift-tuner#311](https://github.com/CANShift/canshift-tuner/pull/311)).
> This page replaces it.

## What you need

- The board and a **USB data cable**. Charge-only cables power the board but
  never show up as a serial port.
- Python 3 and `esptool`:

  ```bash
  pip install esptool        # or: brew install esptool
  ```

  esptool 5 installs the command as `esptool`; on esptool 4, type `esptool.py`
  instead. The subcommands below are spelled the way both versions accept.

- Nothing else holding the port. Close any serial monitor, `screen`, the tuner
  tab, or a second esptool — two processes writing one port corrupts the flash.

## 1. Find your chip family

A release ships one firmware per chip family, not per board. The board is
picked at run time from its profile (see [Board profile](../architecture/board-profile.md)).

| Board                                      | Chip      | USB link                 | Flash | SPIFFS offset |
| ------------------------------------------ | --------- | ------------------------ | ----- | ------------- |
| Waveshare ESP32-S3-Touch-LCD-2.8           | `esp32s3` | native USB, no bridge    | 16 MB | `0x810000`    |
| Elecrow CrowPanel 2.8"                     | `esp32`   | CH340 USB-to-UART bridge | 4 MB  | `0x310000`    |
| Generic ESP32 + ILI9341 (XPT2046 or GT911) | `esp32`   | depends on the board     | 4 MB  | `0x310000`    |

The offsets come from the `spiffs` row of [`ota_16mb.csv`](../../ota_16mb.csv) and
[`ota_4mb.csv`](../../ota_4mb.csv); [Flash layout](../reference/flash-layout.md)
has both tables.

## 2. Download the release

From [Releases](https://github.com/CANShift/canshift-firmware/releases), take two
files for your chip, plus `manifest.json`:

```
canshift-<chip>-vX.Y.Z-merged.bin    bootloader + partition table + app, written at 0x0
canshift-<chip>-vX.Y.Z-spiffs.bin    config, fonts and icons, written at the SPIFFS offset
```

The third file, `-firmware.bin`, is the app alone. It is the OTA payload and is not
needed for a first flash.

Check the downloads against the SHA-256 that `manifest.json` lists for them:

```bash
shasum -a 256 canshift-*-merged.bin canshift-*-spiffs.bin
```

## 3. Find the serial port

Plug the board in, then list the ports.

| OS      | Waveshare S3 (native USB) | CrowPanel (CH340)                                |
| ------- | ------------------------- | ------------------------------------------------ |
| macOS   | `/dev/cu.usbmodem*`       | `/dev/cu.usbserial-*` or `/dev/cu.wchusbserial*` |
| Linux   | `/dev/ttyACM0`            | `/dev/ttyUSB0`                                   |
| Windows | `COMx` in Device Manager  | `COMx` in Device Manager → Ports (COM & LPT)     |

On Linux, add yourself to the `dialout` group (or `uucp` on Arch) if the port
refuses to open. macOS and current Windows ship the CH340 driver.

## 4. Flash

Erase first. A board that ran other firmware keeps its old NVS otherwise, and a
stale board profile or calibration from a previous image can keep the new one
from coming up cleanly.

### Waveshare ESP32-S3-Touch-LCD-2.8

```bash
PORT=/dev/cu.usbmodem1101
TAG=v0.19.0

esptool --chip esp32s3 --port "$PORT" erase_flash
esptool --chip esp32s3 --port "$PORT" --baud 460800 write_flash \
  0x0      "canshift-esp32s3-${TAG}-merged.bin" \
  0x810000 "canshift-esp32s3-${TAG}-spiffs.bin"
```

The S3 talks USB directly, and esptool resets it into the bootloader over the
same link. If it reports `No serial data received` or the port vanishes, put the
chip in download mode by hand: unplug, **hold BOOT while plugging the cable back
in**, release, and run the command again. The port name can change after that;
list it again.

### CrowPanel 2.8" and other ESP32 boards

```bash
PORT=/dev/cu.usbserial-10
TAG=v0.19.0

esptool --chip esp32 --port "$PORT" --baud 460800 erase_flash
esptool --chip esp32 --port "$PORT" --baud 460800 write_flash \
  0x0      "canshift-esp32-${TAG}-merged.bin" \
  0x310000 "canshift-esp32-${TAG}-spiffs.bin"
```

The CH340 resets the chip into the bootloader through its DTR/RTS lines, and that
timing is marginal on this board. If esptool loops on `Connecting...` or reports
`Failed to communicate with the flash chip`:

1. Hold **BOOT**, tap **EN / RST**, release **BOOT**, and run the command at once.
2. Add `--connect-attempts 10` so esptool retries the reset sequence.
3. Drop to `--baud 115200`. Avoid `921600`; it times out on weak cables.

### Why the merged image goes at `0x0` on both chips

`esptool merge_bin` writes the image from offset `0x0` and pads up to the
bootloader. On the ESP32 the bootloader sits at `0x1000` inside that image, on
the S3 at `0x0`. Write the merged file anywhere other than `0x0` and every part
of it lands at the wrong address: esptool reports success, and the board never
boots. [Release process](../contributing/release-process.md) has the details.

## 5. First boot

The board resets when esptool finishes.

1. The splash shows the CANShift mark and the firmware version.
2. **Resistive touch only (CrowPanel, `XPT2046`):** with no calibration in NVS,
   the dash asks for touch calibration — tap each corner crosshair. The result is
   saved and not asked for again. Capacitive boards skip this step.
3. The default dashboard comes up.

Each chip's image boots with the profile of its reference board: CrowPanel 2.8"
for `esp32`, Waveshare 2.8 for `esp32s3`. Any other ESP32 board needs its profile
before its screen and touch line up. Connect the tuner over USB and pick the board,
or provision a profile of your own: [Adding a board](../reference/add-a-board.md).

## It didn't work

| Symptom                                                | Cause and fix                                                                                                  |
| ------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------- |
| No port appears                                        | Charge-only cable, or a hub dropping power. Use a data cable straight into the computer.                       |
| `Permission denied` on the port                        | Linux: join `dialout`. Any OS: another process holds the port.                                                 |
| Stalls mid-write, or `Packet content transfer stopped` | Power dip during the write. No hub, a known-good cable, lower `--baud`, then erase and flash again.            |
| Screen stays black, flash reported success             | The image went to the wrong offset, or the board needs its profile (see above).                                |
| Splash shows, then an error bar                        | The firmware runs; the config or the SPIFFS image is the problem. Flash the `-spiffs.bin` again at its offset. |

Release builds log errors only, so a silent serial port does not mean the board
is dead. The screen is the reliable signal. For the full boot log, build from
source with a higher `APP_LOG_LEVEL` ([Build flags](../reference/build-flags.md)).

## What a full erase costs

`erase_flash` wipes NVS as well as the firmware:

- touch calibration (resistive boards calibrate again on the next boot),
- BLE bonds (pair the phone again from the mobile app),
- the provisioned board profile,
- brightness and the other Settings values,
- the dashboard config on SPIFFS, back to the bundled default.

The ROM bootloader is in mask ROM and no esptool command touches it, so a board
that still enumerates over USB can always be flashed again this way.
