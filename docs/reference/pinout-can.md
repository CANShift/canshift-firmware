# CAN header

Source: [`include/boards/crowpanel_28.h`](../../include/boards/crowpanel_28.h)

CAN runs off the ESP32's built-in TWAI controller, broken out to the expansion header.

| Function | GPIO | Note                 |
| -------- | ---: | -------------------- |
| TWAI TX  |   22 | to the transceiver   |
| TWAI RX  |   21 | from the transceiver |

These two pins belong to the board profile, not to the firmware as a whole: each
board declares its own pair, and the CrowPanel 2.8" values are the ones above.
Check the profile for the board you flash before wiring the transceiver.

## Bus speed

The default is 500 kbit/s (`can.default_speed_kbps`), the most common rate on modern vehicle buses. The profile you load can change what the frames mean, but the bit rate is a firmware setting.

## You still need a transceiver

The GPIO pins are logic-level TWAI — they are **not** a CAN bus on their own. Between them and the vehicle sits a transceiver (the reference build uses an NXP TJA1051), which converts the ESP32's single-ended signals to the differential CAN-H / CAN-L pair. See the [Hardware & BOM](../reference/hardware-bom.md) for the part.

> [!WARNING]
> `TWAI TX` must be an output-capable pin. GPIO 34–39 are input-only on the ESP32 and cannot drive it — keep the header on the pins your board's profile declares (22 / 21 on the CrowPanel 2.8") unless you know the alternative pin can output.
> The full board pinout is on [Pinout](../reference/pinout.md).
