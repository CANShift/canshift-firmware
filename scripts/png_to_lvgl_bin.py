#!/usr/bin/env python3
# png_to_lvgl_bin.py — Convert a PNG into LVGL 8.x .bin (LV_IMG_CF_TRUE_COLOR, RGB565 LE)
#
# Usage: python3 png_to_lvgl_bin.py INPUT.png OUTPUT.bin --width 280 --height 140
#
# The output is a 4-byte LVGL header followed by raw RGB565 little-endian
# pixel data, ready to be loaded with lv_img_set_src("S:/path/to/file.bin").
# Pairs with LV_COLOR_16_SWAP=0 + flush byte-swap in display_driver.cpp.

import argparse
import struct
import sys
from pathlib import Path

from PIL import Image

LV_IMG_CF_TRUE_COLOR = 4


def lvgl_header(cf: int, width: int, height: int) -> bytes:
    if width >= 1 << 11 or height >= 1 << 11:
        raise ValueError("Dimensions must fit in 11 bits each")
    packed = (cf & 0x1F) | ((width & 0x7FF) << 10) | ((height & 0x7FF) << 21)
    return struct.pack("<I", packed)


def rgb888_to_rgb565_le(r: int, g: int, b: int) -> bytes:
    val = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    return struct.pack("<H", val)


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("input", help="Input PNG path")
    p.add_argument("output", help="Output .bin path")
    p.add_argument("--width", type=int, required=True)
    p.add_argument("--height", type=int, required=True)
    args = p.parse_args()

    src = Image.open(args.input).convert("RGB")
    img = src.resize((args.width, args.height), Image.LANCZOS)

    payload = bytearray()
    payload += lvgl_header(LV_IMG_CF_TRUE_COLOR, args.width, args.height)
    for y in range(args.height):
        for x in range(args.width):
            r, g, b = img.getpixel((x, y))
            payload += rgb888_to_rgb565_le(r, g, b)

    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(payload)
    print(f"wrote {out} ({len(payload)} bytes, {args.width}x{args.height})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
