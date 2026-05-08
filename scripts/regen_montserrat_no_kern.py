#!/usr/bin/env python3
# canshift-firmware/scripts/regen_montserrat_no_kern.py
#
# Regenerates Montserrat .c font files used by the firmware, but with
# kerning tables stripped (lv_font_conv --no-kerning). Saves ~3 KB per
# size (~21 KB total across the 7 compiled sizes).
#
# Output: src/ui/fonts/lv_font_montserrat_<N>_nk.c
# Symbol: lv_font_montserrat_<N>_nk (renamed to avoid clashing with the
#         upstream LVGL Montserrat fonts we keep declared in lv_conf.h
#         being disabled — see include/lv_conf.h).
#
# Inputs (Montserrat-Medium.ttf, FontAwesome5-Solid+Brands+Regular.woff)
# are pulled from the LVGL package shipped under .pio/libdeps. Run after
# `pio run` so the package exists.
#
# Run from canshift-firmware/:
#   python3 scripts/regen_montserrat_no_kern.py

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

SIZES = (12, 14, 16, 20, 24, 32, 48)

# Same FontAwesome glyph set the upstream `built_in_font_gen.py` uses,
# kept verbatim to preserve LV_SYMBOL_* renderings.
SYMS = (
    "61441,61448,61451,61452,61452,61453,61457,61459,61461,61465,"
    "61468,61473,61478,61479,61480,61502,61507,61512,61515,61516,"
    "61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,"
    "61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,"
    "61641,61664,61671,61674,61683,61724,61732,61787,61931,62016,"
    "62017,62018,62019,62020,62087,62099,62212,62189,62810,63426,"
    "63650"
)
LATIN_RANGE = "0x20-0x7F,0xB0,0x2022"


def find_font_dir(firmware_dir: Path) -> Path:
    """Locate Montserrat-Medium.ttf inside any LVGL libdep folder."""
    candidates = list(
        firmware_dir.glob(".pio/libdeps/*/lvgl/scripts/built_in_font/Montserrat-Medium.ttf")
    )
    if not candidates:
        sys.exit(
            "Montserrat-Medium.ttf not found under .pio/libdeps/*/lvgl/scripts/built_in_font/.\n"
            "Run `pio run -e crowpanel_28` once first to populate the LVGL package."
        )
    return candidates[0].parent


def regen_one(size: int, font_dir: Path, out_dir: Path) -> Path:
    out_name = f"lv_font_montserrat_{size}_nk.c"
    # Run lv_font_conv from font_dir so the comment header in the
    # output records bare basenames (Montserrat-Medium.ttf, etc.)
    # rather than absolute paths. Output also goes to font_dir so the
    # `-o` flag stays a basename — we move it afterwards.
    cmd = [
        "lv_font_conv",
        "--no-compress",
        "--no-prefilter",
        "--no-kerning",
        "--bpp",
        "4",
        "--size",
        str(size),
        "--font",
        "Montserrat-Medium.ttf",
        "-r",
        LATIN_RANGE,
        "--font",
        "FontAwesome5-Solid+Brands+Regular.woff",
        "-r",
        SYMS,
        "--format",
        "lvgl",
        "-o",
        out_name,
    ]
    subprocess.run(cmd, check=True, cwd=font_dir)
    src = font_dir / out_name
    dst = out_dir / out_name
    src.replace(dst)
    return dst


def post_process(path: Path, size: int) -> None:
    """Rewrite the generated file to use the renamed symbol
    `lv_font_montserrat_<N>_nk` and the lvgl.h include path that
    matches our PlatformIO build (LVGL ships in the include root, not
    under `lvgl/`).

    The internal `#if LV_FONT_MONTSERRAT_<N>_NK` guard the generator
    emits is left intact — its `#ifndef ... #define ... 1` default
    means the body always compiles unless someone explicitly defines
    the macro to 0.
    """
    text = path.read_text()
    # Public lv_font_t object is the only globally-visible identifier.
    # The static file-locals (glyph_bitmap, cmaps, font_dsc, …) keep
    # their original names; no name clash since each font lives in its
    # own translation unit.
    text = text.replace(
        f"lv_font_montserrat_{size} =",
        f"lv_font_montserrat_{size}_nk =",
    )
    # PlatformIO's LVGL package puts lvgl.h at the include root —
    # collapse the two-branch include the generator emits to a plain
    # `#include "lvgl.h"`.
    text = text.replace(
        '#ifdef LV_LVGL_H_INCLUDE_SIMPLE\n'
        '#include "lvgl.h"\n'
        '#else\n'
        '#include "lvgl/lvgl.h"\n'
        '#endif\n',
        '#include "lvgl.h"\n',
    )
    path.write_text(text)


def main() -> int:
    script_dir = Path(__file__).resolve().parent
    firmware_dir = script_dir.parent
    out_dir = firmware_dir / "src" / "ui" / "fonts"
    out_dir.mkdir(parents=True, exist_ok=True)

    font_dir = find_font_dir(firmware_dir)
    print(f"Using TTF/WOFF from: {font_dir}")
    print(f"Writing fonts to:    {out_dir}")

    for size in SIZES:
        out = regen_one(size, font_dir, out_dir)
        post_process(out, size)
        print(f"  generated {out.name} ({out.stat().st_size} B)")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
