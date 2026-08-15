#!/usr/bin/env python3

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

LATIN_RANGE = "0x20-0x7F,0xB0,0xB7,0x2014,0x2022,0x2192"
NUMERIC_RANGE = "0x20,0x2D,0x2E,0x30-0x39"

FAMILIES = (
    ("jbmono", "extrabold", "JetBrainsMono-ExtraBold.ttf", (32, 44, 48), "lvgl", LATIN_RANGE),
    ("jbmono", "extrabold", "JetBrainsMono-ExtraBold.ttf", (84,), "lvgl", NUMERIC_RANGE),
    ("jbmono", "extrabold", "JetBrainsMono-ExtraBold.ttf", (17, 22), "bin", LATIN_RANGE),
    ("archivo", "extrabold", "Archivo-ExtraBold.ttf", (10, 12, 16), "bin", LATIN_RANGE),
    ("archivo", "extrabold", "Archivo-ExtraBold.ttf", (13, 14, 15), "lvgl", LATIN_RANGE),
)

JBMONO_VALUE_FALLBACK = ("jbmono", "medium", "JetBrainsMono-Medium.ttf", (10,), "lvgl", LATIN_RANGE)


def run_lv_font_conv(*args: str) -> None:
    cmd = ["lv_font_conv", "--no-compress", "--no-prefilter", "--no-kerning",
           "--bpp", "4", *args]
    subprocess.run(cmd, check=True)


def collapse_include(path: Path) -> None:
    text = path.read_text()
    text = text.replace(
        '#ifdef LV_LVGL_H_INCLUDE_SIMPLE\n'
        '#include "lvgl.h"\n'
        '#else\n'
        '#include "lvgl/lvgl.h"\n'
        '#endif\n',
        '#include "lvgl.h"\n',
    )
    path.write_text(text)


def regen(input_dir: Path, bins_dir: Path, src_dir: Path) -> None:
    bins_dir.mkdir(parents=True, exist_ok=True)
    src_dir.mkdir(parents=True, exist_ok=True)
    for family, weight, ttf_name, sizes, fmt, char_range in (*FAMILIES, JBMONO_VALUE_FALLBACK):
        ttf = input_dir / ttf_name
        if not ttf.exists():
            sys.exit(f"Missing TTF: {ttf} — see data/fonts/SOURCES.md")
        for size in sizes:
            if fmt == "bin":
                out = bins_dir / f"{family}_{weight}_{size}.bin"
            else:
                out = src_dir / f"lv_font_{family}_{weight}_{size}_nk.c"
            run_lv_font_conv(
                "--size", str(size),
                "--font", str(ttf),
                "-r", char_range,
                "--format", fmt,
                "-o", str(out),
            )
            if fmt == "lvgl":
                collapse_include(out)
            print(f"  generated {out.name} ({out.stat().st_size} B)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input_dir", type=Path,
                        help="Directory holding the static TTF instances named per FAMILIES")
    args = parser.parse_args()
    if not args.input_dir.is_dir():
        sys.exit(f"Not a directory: {args.input_dir}")

    firmware_dir = Path(__file__).resolve().parent.parent
    regen(args.input_dir, firmware_dir / "data" / "fonts", firmware_dir / "src" / "ui" / "fonts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
