# JetBrains Mono — Firmware bin fonts

LVGL `.bin` fonts loaded at boot from SPIFFS by `FontManager::init()`
(see `src/ui/font_manager.cpp`). Latin ASCII only (`0x20-0x7F`), bpp=4,
no kerning. Replaced Orbitron in the #1838 restyle (#1821): all values the
car produces render in monospace with tabular figures.

## Provenance

- Upstream: https://github.com/google/fonts/tree/main/ofl/jetbrainsmono
- Variable source: `JetBrainsMono[wght].ttf` (weight axis 100-800)
- Static instances at wght=500 / 700 / 800 produced via
  `fonttools varLib.instancer 'JetBrainsMono[wght].ttf' wght=<W>`.
- Conversion: `lv_font_conv --size <S> --bpp 4 --no-kerning --range 0x20-0x7F`
  (`--format bin` for SPIFFS, `--format lvgl` for the in-flash twins).

## Weight / size matrix

| Intent     | Weight          | Sizes (px)        | Files                                                |
| ---------- | --------------- | ----------------- | ---------------------------------------------------- |
| primary    | ExtraBold (800) | 32, 48            | in-flash twins only — no SPIFFS .bin                 |
| secondary  | Bold (700)      | 20, 24            | `jbmono_bold_{20,24}.bin`                            |
| label      | Medium (500)    | 8, 10, 12, 14, 16 | `jbmono_medium_{8,10,12,14,16}.bin` (14 is in-flash) |

## In-flash twins

- `src/ui/fonts/lv_font_jbmono_medium_14_nk.c` — FontManager fallback when a
  SPIFFS load fails (fresh-flash device without `pio run -t uploadfs`).
- `src/ui/fonts/lv_font_jbmono_extrabold_32_nk.c` and `_48_nk.c` — both
  primary sizes ship as compiled C arrays; the 80 KB LVGL pool cannot host
  them alongside the draw buffers (see PR #665 for the original rationale).
