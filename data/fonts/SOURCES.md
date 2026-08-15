# Firmware bin fonts — JetBrains Mono (values) + Archivo (labels)

LVGL `.bin` fonts loaded at boot from SPIFFS by `FontManager::init()`
(see `src/ui/font_manager.cpp`). Latin ASCII plus `°`, `·`, `•` and `→`
(`0x20-0x7F,0xB0,0xB7,0x2022,0x2192`), bpp=4,
no kerning. Replaced Orbitron in the #1838 restyle (#1821): all values the
car produces render in monospace with tabular figures.

## Provenance

- JetBrains Mono: https://github.com/google/fonts/tree/main/ofl/jetbrainsmono —
  static instances at wght=500 / 700 / 800 via
  `fonttools varLib.instancer 'JetBrainsMono[wght].ttf' wght=<W>`.
- Archivo: https://github.com/google/fonts/tree/main/ofl/archivo — static
  ExtraBold (800) instance, same OFL licence.
- Conversion: `scripts/regen_fonts.py <ttf-dir>` — wraps
  `lv_font_conv --size <S> --bpp 4 --no-kerning --range 0x20-0x7F,0xB0,0xB7,0x2022,0x2192`
  (`--format bin` for SPIFFS, `--format lvgl` for the in-flash twins).

## Weight / size matrix

| Intent    | Family / weight          | Sizes (px, device)   | Files                                              |
| --------- | ------------------------ | -------------------- | -------------------------------------------------- |
| value     | JB Mono ExtraBold (800)  | 32, 44, 48, 84       | in-flash twins only — no SPIFFS .bin               |
| value     | JB Mono ExtraBold (800)  | 17, 22               | `jbmono_extrabold_{17,22}.bin`                     |
| units     | JB Mono Medium (500)     | 10                   | in-flash (`lv_font_jbmono_medium_10_nk.c`)         |
| label     | Archivo ExtraBold (800)  | 10, 12, 13, 14, 15, 16 | `archivo_extrabold_{10,12,16}.bin` (13/14/15 in-flash) |

Sizes follow `docs/design/DASH_DESIGN_SYSTEM.md` §3 (device scale: the references are 2×):
hero 48, heroTrack 44, primary 32, mid 22, secondary 17, button 14, kicker/unit/status 10.
The size class comes from the widget's `big` property via `WidgetHelpers::deviceFontPxForBig`,
keyed on the canvas value the Tuner writes (96 / 88 / 64 / 44, anything else secondary).

The critical-alert takeover (§10) sits outside that ladder: signal name 13, value 84,
call to action 15. All three are in-flash so the screen renders on a device that was
flashed without `pio run -t uploadfs`. The 84 px face carries digits, `.`, `-` and space
only (`NUMERIC_RANGE`) — it is the takeover value and nothing else.

## In-flash twins

- `src/ui/fonts/lv_font_jbmono_medium_14_nk.c` — value-tier fallback when a
  SPIFFS load fails (fresh-flash device without `pio run -t uploadfs`).
- `src/ui/fonts/lv_font_archivo_extrabold_14_nk.c` — label-tier fallback,
  same situation.
- `src/ui/fonts/lv_font_jbmono_extrabold_32_nk.c` and `_48_nk.c` — both
  primary sizes ship as compiled C arrays; the 80 KB LVGL pool cannot host
  them alongside the draw buffers (see PR #665 for the original rationale).
