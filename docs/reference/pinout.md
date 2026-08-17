# Pinout — CrowPanel 2.8

Source: [`include/boards/crowpanel_28.h`](../../include/boards/crowpanel_28.h)

| Function  | GPIO | Note                                     |
| --------- | ---: | ---------------------------------------- |
| TFT MOSI  |   13 | HSPI                                     |
| TFT MISO  |   12 | unused in practice (panel write-only)    |
| TFT SCLK  |   14 | HSPI                                     |
| TFT CS    |   15 |                                          |
| TFT DC    |    2 | Data/Command (RS)                        |
| TFT RST   |    — | not wired (held high internally)         |
| TFT BL    |   27 | PWM backlight, 0–255                     |
| Touch CS  |   33 | XPT2046, shares the SPI bus with the TFT |
| Touch IRQ |    — | polling via `getTouch()` — no IRQ        |
| TWAI TX   |   22 | CAN — expansion header                   |
| TWAI RX   |   21 | CAN — expansion header                   |

## SPI clocks

- **TFT**: 27 MHz, from `lcd.freq_write_hz` in the board profile
  (`include/boards/crowpanel_28.h`). Change it there, per board — there is no
  global build flag for it.
- **Touch**: 2.5 MHz (XPT2046 max).

## Free pins

GPIO 16/17 (UART2) are intentionally left free for expansion. GPIO 21/22 are the
I²C header pins on the bare module, but this board's profile claims them for
TWAI — they are not free.

## Avoid

GPIO 6–11 are reserved for the internal flash SPI — **never** wire anything to
them. GPIO 34–39 are input-only and cannot host TWAI TX (which must be
bidirectional).

Every pin on this page comes from the board profile the firmware compiles
against — `canshift-firmware/include/boards/crowpanel_28.h`. Another board
declares its own, so read that board's profile rather than assuming these.
