# Display bus

Source: [`include/boards/crowpanel_28.h`](../../include/boards/crowpanel_28.h)

The panel and its touch controller sit on the same SPI bus (HSPI). They share the clock and data lines and are told apart by their chip-select pins — so nothing else can hang off this bus.

| Function  | GPIO | Note                                          |
| --------- | ---: | --------------------------------------------- |
| TFT MOSI  |   13 | shared HSPI                                   |
| TFT MISO  |   12 | shared HSPI — unused, the panel is write-only |
| TFT SCLK  |   14 | shared HSPI                                   |
| TFT CS    |   15 | ILI9341 chip select                           |
| TFT DC    |    2 | data/command select                           |
| TFT RST   |    — | not wired (`-1`), held high on the board      |
| TFT BL    |   27 | PWM backlight                                 |
| Touch CS  |   33 | XPT2046 chip select                           |
| Touch IRQ |    — | not wired (`-1`) — touch is polled            |

## Clocks

The two devices run the bus at different speeds:

- **TFT** — 27 MHz by default (`lcd.freq_write_hz`), or 40 MHz when built with `-DHW_TFT_FAST_SPI=1`, which is off by default and worth validating on your own board first.
- **Touch** — 2.5 MHz (`touch.freq_hz`), the XPT2046's ceiling.

## Touch is polled, not interrupt-driven

`touch.pin_irq` is `-1` — the touch IRQ line isn't wired. The firmware reads the controller by polling rather than waiting on an interrupt, so there is no pin to reserve for it.

## Backlight

`lcd.pin_bl` (GPIO 27) drives the backlight through an LEDC PWM channel — channel 0 at 5 kHz, with the duty running 0–255 (default 200). The firmware owns the duty, so brightness is a software setting, not a fixed resistor.

The full board pinout, including the free and reserved pins, is on [Pinout](../reference/pinout.md).
