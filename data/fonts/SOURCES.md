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

| Intent     | Weight       | Sizes (px) | Files                                                  |
| ---------- | ------------ | ---------- | ------------------------------------------------------ |
| primary    | Black (900)  | 32, 48     | both in-flash twins (see below) — no SPIFFS .bin       |
| secondary  | Bold (700)   | 20, 24     | `orbitron_bold_{20,24}.bin`                            |
| label      | Medium (500) | 8, 10, 12, 14, 16 | `orbitron_medium_{8,10,12,14,16}.bin` (14 is in-flash) |

## In-flash twins

Three Orbitron faces are linked into flash as compiled C arrays instead of
loaded from SPIFFS:

- `src/ui/fonts/lv_font_orbitron_medium_14_nk.c` — symbol
  `lv_font_orbitron_medium_14_nk`. Acts as the FontManager fallback when a
  SPIFFS load fails (e.g. fresh-flash device without `pio run -t uploadfs`).
- `src/ui/fonts/lv_font_orbitron_black_32_nk.c` — symbol
  `lv_font_orbitron_black_32_nk`. Moved in-flash in PR #665 to free ~20 KB
  of LVGL pool.
- `src/ui/fonts/lv_font_orbitron_black_48_nk.c` — symbol
  `lv_font_orbitron_black_48_nk`. Moved in-flash in PR #665 because hosting
  the 43 KB binary in the LVGL pool is impossible: the 80 KB pool is shared
  with the two LVGL draw buffers (~25 KB combined) and widget runtime state,
  leaving roughly 50 KB available for fonts. Black 32 + Black 48 together
  would exhaust that budget by themselves — both primary sizes therefore
  ship as compiled C arrays and the SPIFFS-loaded set shrinks to bold/medium
  only.

## Dropped

The 28 px Bold and 48 px Black entries were dropped in PR #487 to fit the
then-active LV_MEM_SIZE=64KB budget. PR #665 (issue #664) restores
**48 px Black** via in-flash linkage. **28 px Bold remains dropped** because
even with both primary sizes off the pool, adding 28 px Bold (15 KB) on top
of the existing bold/medium SPIFFS budget leaves the pool too tight for
LVGL widget runtime state. Secondary text that previously asked for 28
snaps down to 24 instead.

## Conversion command — SPIFFS bin

```bash
lv_font_conv --no-compress --no-prefilter --no-kerning --bpp 4 \
  --size <N> --font Orbitron-<Weight>.ttf \
  -r 0x20-0x7F,0xB0,0x2022 --format bin -o orbitron_<weight>_<N>.bin
```

The `0xB0` (degree sign) and `0x2022` (bullet) extras keep parity with the
previous Montserrat range — both are used by widget labels.

## Conversion command — in-flash LVGL C

For the in-flash twins, swap `--format bin` for `--format lvgl` and target
the firmware fonts directory. Requirements: `npm i -g lv_font_conv` (needs
Node ≥ 16 — older Node WASM build fails) and a fonttools-instanced static
TTF. Full recipe:

```bash
# 1. Download the pinned variable TTF.
curl -L -o /tmp/Orbitron-VF.ttf \
  "https://raw.githubusercontent.com/google/fonts/8b0a1d0f5983c89bc2b93f1b5fb55f9e252744b5/ofl/orbitron/Orbitron%5Bwght%5D.ttf"

# 2. Instance to the desired weight (900 = Black, 500 = Medium).
python3 -m fontTools.varLib.instancer /tmp/Orbitron-VF.ttf wght=900 \
  -o /tmp/Orbitron-Black.ttf

# 3. Convert to LVGL in-flash C.
lv_font_conv --no-compress --no-prefilter --no-kerning --bpp 4 \
  --size 32 --font /tmp/Orbitron-Black.ttf \
  -r 0x20-0x7F,0xB0,0x2022 --format lvgl \
  -o canshift-firmware/src/ui/fonts/lv_font_orbitron_black_32_nk.c
```

## License

`OFL.txt` (this directory) — SIL Open Font License 1.1.
Shipped to the device along with the bins so the OFL clause 5 redistribution
requirement is satisfied with the firmware artifact.
