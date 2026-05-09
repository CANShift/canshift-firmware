# Orbitron — Firmware bin fonts

LVGL `.bin` fonts loaded at boot from SPIFFS by `FontManager::init()`
(see `src/ui/font_manager.cpp`). Latin ASCII only (`0x20-0x7F`), bpp=4,
no kerning.

## Provenance

- Upstream: https://github.com/google/fonts/tree/main/ofl/orbitron
- Pinned commit: `8b0a1d0f5983c89bc2b93f1b5fb55f9e252744b5` (2026-03-12)
- Variable source: `Orbitron[wght].ttf` (weight axis 400–900)
- Static instances at wght=500 / 700 / 900 produced via
  `fonttools varLib.instancer Orbitron[wght].ttf wght=<W>`.

## Weight / size matrix

| Intent     | Weight       | Sizes (px) | Files                                              |
| ---------- | ------------ | ---------- | -------------------------------------------------- |
| primary    | Black (900)  | 32, 48     | `orbitron_black_32.bin`, `orbitron_black_48.bin`   |
| secondary  | Bold (700)   | 20, 24, 28 | `orbitron_bold_{20,24,28}.bin`                     |
| label      | Medium (500) | 12, 14, 16 | `orbitron_medium_{12,14,16}.bin`                   |

The 28-px Bold entry is required by the boot/burn overlay icon (`burn_overlay.cpp`).
The 14-px Medium also has an in-flash twin
(`canshift-firmware/src/ui/fonts/lv_font_orbitron_medium_14_nk.c`,
symbol `lv_font_orbitron_medium_14_nk`) used as the FontManager fallback when a
SPIFFS load fails (e.g. fresh-flash device without `pio run -t uploadfs`).

## Conversion command

```bash
lv_font_conv --no-compress --no-prefilter --no-kerning --bpp 4 \
  --size <N> --font Orbitron-<Weight>.ttf \
  -r 0x20-0x7F,0xB0,0x2022 --format bin -o orbitron_<weight>_<N>.bin
```

The `0xB0` (degree sign) and `0x2022` (bullet) extras keep parity with the
previous Montserrat range — both are used by widget labels.

## License

`OFL.txt` (this directory) — SIL Open Font License 1.1.
Shipped to the device along with the bins so the OFL clause 5 redistribution
requirement is satisfied with the firmware artifact.
