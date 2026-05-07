#!/usr/bin/env python3
# build_sensor_icons.py — Render every sensor SVG → PNG → LVGL .bin
#
# Pipeline:
#   icon_sources/sensors/<name>.svg
#     → render via rsvg-convert (white stroke, transparent bg, 32×32 PNG)
#     → png_to_lvgl_bin.py --alpha (LV_IMG_CF_TRUE_COLOR_ALPHA, 32×32)
#     → sd_contents/assets/sensor_<name>.bin
#
# The output paths must match canshift-firmware/src/ui/icon_assets.cpp.
#
# Requirements: rsvg-convert (`brew install librsvg`) + Pillow.

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ICON_SIZE = 32  # px — matches the alert/bar widget icon footprint

SCRIPTS_DIR = Path(__file__).resolve().parent
SVG_DIR = SCRIPTS_DIR / "icon_sources" / "sensors"
SD_ASSETS = SCRIPTS_DIR.parent / "sd_contents" / "assets"
PNG_TO_BIN = SCRIPTS_DIR / "png_to_lvgl_bin.py"


def require(cmd: str) -> None:
    if shutil.which(cmd) is None:
        raise SystemExit(
            f"ERROR: `{cmd}` not found on PATH. Install with `brew install librsvg`."
        )


def render_svg_to_png(svg: Path, png: Path) -> None:
    subprocess.run(
        [
            "rsvg-convert",
            "--width", str(ICON_SIZE),
            "--height", str(ICON_SIZE),
            "--keep-aspect-ratio",
            "--background-color", "transparent",
            "--output", str(png),
            str(svg),
        ],
        check=True,
    )


def convert_png_to_bin(png: Path, bin_out: Path) -> None:
    subprocess.run(
        [
            sys.executable,
            str(PNG_TO_BIN),
            str(png),
            str(bin_out),
            "--width", str(ICON_SIZE),
            "--height", str(ICON_SIZE),
            "--alpha",
        ],
        check=True,
    )


def main() -> int:
    require("rsvg-convert")
    if not SVG_DIR.is_dir():
        raise SystemExit(f"ERROR: source dir not found: {SVG_DIR}")

    SD_ASSETS.mkdir(parents=True, exist_ok=True)
    svgs = sorted(SVG_DIR.glob("*.svg"))
    if not svgs:
        raise SystemExit(f"ERROR: no SVGs found in {SVG_DIR}")

    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        for svg in svgs:
            name = svg.stem
            png = tmp_path / f"sensor_{name}.png"
            bin_out = SD_ASSETS / f"sensor_{name}.bin"
            render_svg_to_png(svg, png)
            convert_png_to_bin(png, bin_out)

    print(f"\n{len(svgs)} icons written to {SD_ASSETS}/")
    return 0


if __name__ == "__main__":
    sys.exit(main())
