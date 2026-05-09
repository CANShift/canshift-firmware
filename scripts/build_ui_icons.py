#!/usr/bin/env python3
# build_ui_icons.py — Render every UI SVG → PNG → LVGL .bin
#
# Pipeline:
#   icon_sources/ui/<name>.svg
#     → render via rsvg-convert (white stroke, transparent bg, 12×12 PNG)
#     → png_to_lvgl_bin.py --alpha (LV_IMG_CF_TRUE_COLOR_ALPHA, 12×12)
#     → data/assets/icon_<name>.bin
#
# Sibling of build_sensor_icons.py — UI icons (day/night toggle, etc.) are
# rendered at 12×12 with cf=5 so the runtime can tint opaque pixels without
# losing transparency, matching the sensor icon pipeline.
#
# Output paths must match canshift-firmware/src/ui/top_bar.cpp consumers
# (lv_img_set_src("S:/assets/icon_<name>.bin")).
#
# Requirements: rsvg-convert (`brew install librsvg`) + Pillow.

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ICON_SIZE = 12  # px — matches the top-bar day/night badge footprint

SCRIPTS_DIR = Path(__file__).resolve().parent
SVG_DIR = SCRIPTS_DIR / "icon_sources" / "ui"
ASSETS_DIR = SCRIPTS_DIR.parent / "data" / "assets"
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

    ASSETS_DIR.mkdir(parents=True, exist_ok=True)
    svgs = sorted(SVG_DIR.glob("*.svg"))
    if not svgs:
        raise SystemExit(f"ERROR: no SVGs found in {SVG_DIR}")

    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        for svg in svgs:
            name = svg.stem
            png = tmp_path / f"icon_{name}.png"
            bin_out = ASSETS_DIR / f"icon_{name}.bin"
            render_svg_to_png(svg, png)
            convert_png_to_bin(png, bin_out)

    print(f"\n{len(svgs)} icons written to {ASSETS_DIR}/")
    return 0


if __name__ == "__main__":
    sys.exit(main())
