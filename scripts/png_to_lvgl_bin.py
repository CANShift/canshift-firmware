#!/usr/bin/env python3

import argparse
import struct
import sys
from pathlib import Path

from PIL import Image

LV_IMG_CF_TRUE_COLOR = 4
LV_IMG_CF_TRUE_COLOR_ALPHA = 5


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
    p.add_argument(
        "--alpha",
        action="store_true",
        help="Emit LV_IMG_CF_TRUE_COLOR_ALPHA (preserves transparency for runtime recolor)",
    )
    args = p.parse_args()

    mode = "RGBA" if args.alpha else "RGB"
    src = Image.open(args.input).convert(mode)
    img = src.resize((args.width, args.height), Image.LANCZOS)

    cf = LV_IMG_CF_TRUE_COLOR_ALPHA if args.alpha else LV_IMG_CF_TRUE_COLOR
    payload = bytearray()
    payload += lvgl_header(cf, args.width, args.height)
    for y in range(args.height):
        for x in range(args.width):
            px = img.getpixel((x, y))
            if args.alpha:
                r, g, b, a = px
                payload += rgb888_to_rgb565_le(r, g, b)
                payload += struct.pack("<B", a)
            else:
                r, g, b = px
                payload += rgb888_to_rgb565_le(r, g, b)

    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(payload)
    print(f"wrote {out} ({len(payload)} bytes, {args.width}x{args.height}, cf={cf})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
