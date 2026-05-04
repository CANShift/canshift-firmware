#!/usr/bin/env python3
# flash_sd.py — Copy sd_contents/ to a mounted SD card
#
# Usage (manual):
#   python3 scripts/flash_sd.py /Volumes/CANSHIFT
#
# Usage (PlatformIO custom target via `pio run -t flash_sd`):
#   Automatically detects the SD card by looking for /Volumes/* with a
#   recognizable name or prompts the user to pass the mount point.
#
# What gets copied:
#   sd_contents/fonts/   → <SD>/fonts/   (Montserrat .bin files for LVGL)
#   sd_contents/config/  → <SD>/config/  (dashboard.json, signals.json, theme.json)

import sys
import os
import shutil
import argparse

SD_CONTENTS = os.path.join(os.path.dirname(__file__), "..", "sd_contents")


def find_sd_mount():
    """Try to auto-detect SD card mount on macOS under /Volumes."""
    volumes = "/Volumes"
    if not os.path.isdir(volumes):
        return None
    candidates = [
        os.path.join(volumes, v)
        for v in os.listdir(volumes)
        if not v.startswith(".")
        and v not in ("Macintosh HD",)
        and os.path.isdir(os.path.join(volumes, v))
    ]
    if len(candidates) == 1:
        return candidates[0]
    return None


def copy_to_sd(mount: str):
    src = os.path.abspath(SD_CONTENTS)
    if not os.path.isdir(src):
        print(f"ERROR: sd_contents/ not found at {src}")
        sys.exit(1)
    if not os.path.isdir(mount):
        print(f"ERROR: SD mount point not found: {mount}")
        sys.exit(1)

    total = 0
    for root, dirs, files in os.walk(src):
        rel = os.path.relpath(root, src)
        dst_dir = os.path.join(mount, rel)
        os.makedirs(dst_dir, exist_ok=True)
        for f in files:
            src_file = os.path.join(root, f)
            dst_file = os.path.join(dst_dir, f)
            shutil.copy2(src_file, dst_file)
            size = os.path.getsize(src_file)
            print(f"  copied {os.path.join(rel, f)} ({size} bytes)")
            total += size

    print(f"\nDone — {total} bytes written to {mount}")


def main():
    parser = argparse.ArgumentParser(description="Flash sd_contents/ to SD card")
    parser.add_argument("mount", nargs="?", help="SD card mount point (e.g. /Volumes/CANSHIFT)")
    args = parser.parse_args()

    mount = args.mount
    if not mount:
        mount = find_sd_mount()
        if mount:
            print(f"Auto-detected SD card: {mount}")
        else:
            print("ERROR: Cannot auto-detect SD card. Pass mount point as argument.")
            print("  Usage: python3 scripts/flash_sd.py /Volumes/YOURSD")
            sys.exit(1)

    print(f"Copying sd_contents/ → {mount}")
    copy_to_sd(mount)


if __name__ == "__main__":
    main()
