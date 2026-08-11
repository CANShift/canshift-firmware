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

| Intent    | Family / weight          | Sizes (px, device)  | Files                                              |
| --------- | ------------------------ | ------------------- | -------------------------------------------------- |
| value     | JB Mono ExtraBold (800)  | 32, 40, 44, 48      | in-flash twins only — no SPIFFS .bin               |
| value     | JB Mono ExtraBold (800)  | 17, 22, 24          | `jbmono_extrabold_{17,22,24}.bin`                  |
| units     | JB Mono Medium (500)     | 10                  | in-flash (`lv_font_jbmono_medium_10_nk.c`)         |
| label     | Archivo ExtraBold (800)  | 10, 12, 14, 16      | `archivo_extrabold_{10,12,16}.bin` (14 in-flash)   |

Sizes follow `docs/design/PROMPT_DASH_IN_TUNER.md` (device scale: the canvas is 2×):
hero 48/44/40, primary 32, mid 24/22, secondary 17, unit/status 10. The size class
comes from the widget's `big` property via `WidgetHelpers::deviceFontPxForBig`.

## In-flash twins

- `src/ui/fonts/lv_font_jbmono_medium_14_nk.c` — value-tier fallback when a
  SPIFFS load fails (fresh-flash device without `pio run -t uploadfs`).
- `src/ui/fonts/lv_font_archivo_extrabold_14_nk.c` — label-tier fallback,
  same situation.
- `src/ui/fonts/lv_font_jbmono_extrabold_32_nk.c` and `_48_nk.c` — both
  primary sizes ship as compiled C arrays; the 80 KB LVGL pool cannot host
  them alongside the draw buffers (see PR #665 for the original rationale).
