#!/usr/bin/env python3
# canshift-firmware/scripts/regen_orbitron_fonts.py
#
# Regenerates the Orbitron font set (issue #431):
#   - SPIFFS .bin files under data/fonts/      (loaded at boot by FontManager)
#   - One in-flash .c source under src/ui/fonts/lv_font_orbitron_medium_14_nk.c
#     (used as the FontManager fallback when SPIFFS load fails — also
#     `LV_FONT_DEFAULT` in include/lv_conf.h)
#
# Three weights × the actually-consumed sizes — the matrix mirrors
# `FontManager::primary/secondary/label` in src/ui/font_manager.cpp.
#
# Inputs are pulled from a writable tmp dir. Provide the static TTFs (one per
# weight) instanced from the upstream Orbitron variable font ahead of time —
# see canshift-firmware/data/fonts/SOURCES.md for the pinned commit and the
# `fonttools varLib.instancer` invocations.
#
# Run from canshift-firmware/:
#   python3 scripts/regen_orbitron_fonts.py /path/to/orbitron-static-ttfs

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

LATIN_RANGE = "0x20-0x7F,0xB0,0x2022"  # ASCII + degree + bullet (label parity)

# (intent_token_in_filename, weight_filename, sizes)
TIERS = (
    ("black",  "Orbitron-Black.ttf",  (32, 48)),
    ("bold",   "Orbitron-Bold.ttf",   (20, 24, 28)),
    ("medium", "Orbitron-Medium.ttf", (8, 10, 12, 14, 16)),
)


def run_lv_font_conv(*args: str) -> None:
    cmd = ["lv_font_conv", "--no-compress", "--no-prefilter", "--no-kerning",
           "--bpp", "4", *args]
    subprocess.run(cmd, check=True)


def regen_bins(input_dir: Path, output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    for token, ttf_name, sizes in TIERS:
        ttf = input_dir / ttf_name
        if not ttf.exists():
            sys.exit(f"Missing TTF: {ttf} — see data/fonts/SOURCES.md")
        for size in sizes:
            out = output_dir / f"orbitron_{token}_{size}.bin"
            run_lv_font_conv(
                "--size", str(size),
                "--font", str(ttf),
                "-r", LATIN_RANGE,
                "--format", "bin",
                "-o", str(out),
            )
            print(f"  generated {out.name} ({out.stat().st_size} B)")


def regen_in_flash_fallback(input_dir: Path, src_dir: Path) -> None:
    src_dir.mkdir(parents=True, exist_ok=True)
    out = src_dir / "lv_font_orbitron_medium_14_nk.c"
    run_lv_font_conv(
        "--size", "14",
        "--font", str(input_dir / "Orbitron-Medium.ttf"),
        "-r", LATIN_RANGE,
        "--format", "lvgl",
        "-o", str(out),
    )
    # Collapse the two-branch include the generator emits to plain `lvgl.h`
    # (PlatformIO's LVGL package ships the header at the include root).
    text = out.read_text()
    text = text.replace(
        '#ifdef LV_LVGL_H_INCLUDE_SIMPLE\n'
        '#include "lvgl.h"\n'
        '#else\n'
        '#include "lvgl/lvgl.h"\n'
        '#endif\n',
        '#include "lvgl.h"\n',
    )
    out.write_text(text)
    print(f"  generated {out.name} ({out.stat().st_size} B)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "input_dir",
        type=Path,
        help="Directory containing Orbitron-{Medium,Bold,Black}.ttf static instances",
    )
    args = parser.parse_args()

    if not args.input_dir.is_dir():
        sys.exit(f"Not a directory: {args.input_dir}")

    firmware_dir = Path(__file__).resolve().parent.parent
    bins_dir = firmware_dir / "data" / "fonts"
    src_dir = firmware_dir / "src" / "ui" / "fonts"

    print(f"Reading TTFs from: {args.input_dir}")
    print(f"Writing bins to:   {bins_dir}")
    print(f"Writing fallback:  {src_dir}")

    regen_bins(args.input_dir, bins_dir)
    regen_in_flash_fallback(args.input_dir, src_dir)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
