#!/usr/bin/env python3

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ICON_SIZE = 32  # px — matches the alert/bar widget icon footprint

SCRIPTS_DIR = Path(__file__).resolve().parent
SVG_DIR = SCRIPTS_DIR / "icon_sources" / "sensors"
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
            png = tmp_path / f"sensor_{name}.png"
            bin_out = ASSETS_DIR / f"sensor_{name}.bin"
            render_svg_to_png(svg, png)
            convert_png_to_bin(png, bin_out)

    print(f"\n{len(svgs)} icons written to {ASSETS_DIR}/")
    return 0


if __name__ == "__main__":
    sys.exit(main())
