# Firmware bin fonts — JetBrains Mono (values) + Archivo (labels)

LVGL `.bin` fonts loaded at boot from SPIFFS by `FontManager::init()`
(see `src/ui/font_manager.cpp`). Latin ASCII only (`0x20-0x7F`), bpp=4,
no kerning. Replaced Orbitron in the #1838 restyle (#1821): all values the
car produces render in monospace with tabular figures.

## Provenance

- JetBrains Mono: https://github.com/google/fonts/tree/main/ofl/jetbrainsmono —
  static instances at wght=500 / 700 / 800 via
  `fonttools varLib.instancer 'JetBrainsMono[wght].ttf' wght=<W>`.
- Archivo: https://github.com/google/fonts/tree/main/ofl/archivo — static
  ExtraBold (800) instance, same OFL licence.
- Conversion: `scripts/regen_fonts.py <ttf-dir>` — wraps
  `lv_font_conv --size <S> --bpp 4 --no-kerning --range 0x20-0x7F,0xB0,0x2022`
  (`--format bin` for SPIFFS, `--format lvgl` for the in-flash twins).

## Weight / size matrix

| Intent    | Family / weight          | Sizes (px)     | Files                                              |
| --------- | ------------------------ | -------------- | -------------------------------------------------- |
| primary   | JB Mono ExtraBold (800)  | 72             | in-flash twin only — no SPIFFS .bin                |
| danger    | JB Mono ExtraBold (800)  | 46             | in-flash twin only — no SPIFFS .bin                |
| secondary | JB Mono Bold (700)       | 34             | `jbmono_bold_34.bin`                               |
| units     | JB Mono Medium (500)     | 14             | in-flash (`lv_font_jbmono_medium_14_nk.c`)         |
| label     | Archivo ExtraBold (800)  | 10, 12, 14, 16, 28 | `archivo_extrabold_{10,12,16,28}.bin` (14 in-flash) |

Sizes follow `PROMPT_DASH_DESIGN.md`: primary value 72 px, danger 46 px,
secondary 34 px, units 14–15 px muted inline. The 56 px OTA percent lands
with firmware #7.

Labels never render below 10 px — the mockups set the floor at a 9 px
equivalent, so a request for 8 snaps up to 10.

## In-flash twins

- `src/ui/fonts/lv_font_jbmono_medium_14_nk.c` — value-tier fallback when a
  SPIFFS load fails (fresh-flash device without `pio run -t uploadfs`).
- `src/ui/fonts/lv_font_archivo_extrabold_14_nk.c` — label-tier fallback,
  same situation.
- `src/ui/fonts/lv_font_jbmono_extrabold_32_nk.c` and `_48_nk.c` — both
  primary sizes ship as compiled C arrays; the 80 KB LVGL pool cannot host
  them alongside the draw buffers (see PR #665 for the original rationale).
