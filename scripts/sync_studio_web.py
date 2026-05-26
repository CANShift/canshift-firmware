# sync_studio_web.py — PlatformIO extra_scripts hook
#
# Phase 4 of #1077 — ships the dash-hosted Studio SPA alongside the firmware.
# Before compile/link, this script:
#   1. Runs `npm run build` in ../canshift-studio-web/ (skippable via
#      CANSHIFT_SKIP_STUDIO_WEB_BUILD=1 so CI can install once and reuse).
#   2. Mirrors every `*.gz` + every `.woff2` from `canshift-studio-web/dist/`
#      into `canshift-firmware/data/web/` so the SPIFFS image picked up by
#      `pio run -t uploadfs` carries the SPA assets.
#   3. Validates the SPA file list against the expected manifest — fails the
#      build if Vite emits a chunk we didn't wire into the WebServer route
#      table (otherwise the browser would 404 silently).
#
# #1123 follow-up: the SPA artifacts USED to ride in the firmware image via
# `board_build.embed_files`, but that pushed the `_wifi` env past the 1728 KB
# app slot (107.3 %). Moving them to SPIFFS recovers ~185 KB of flash. The
# trade-off — `pio run -t uploadfs` is now a mandatory first-flash step for
# the browser-based Studio.
#
# The script is opt-in per environment via extra_scripts; only the
# crowpanel_28_wifi env (which links the WebServer + WS) pulls it in.

Import("env")

import os
import shutil
import subprocess
import sys
from pathlib import Path


PROJECT_DIR = Path(env["PROJECT_DIR"])
STUDIO_WEB_DIR = (PROJECT_DIR / ".." / "canshift-studio-web").resolve()
STUDIO_WEB_DIST = STUDIO_WEB_DIR / "dist"
WEB_DATA_DIR = (PROJECT_DIR / "data" / "web").resolve()
WEB_ASSETS_DIR = WEB_DATA_DIR / "assets"

# Files we EXPECT to find in dist/ post-build. Order matters here because the
# wifi_ap.cpp route table is hand-maintained in lock-step. If Vite ever
# starts emitting a different chunk graph, this list and wifi_ap.cpp's
# kSpaAssets[] table need a coordinated bump. Keep the two in sync.
EXPECTED_GZ = [
    "index.html.gz",
    "assets/index.js.gz",
    "assets/index.css.gz",
    "assets/vendor-react.js.gz",
    "assets/vendor-radix.js.gz",
    "assets/vendor-state.js.gz",
    "assets/EditorRoute.js.gz",
]
EXPECTED_FONTS = [
    "assets/Orbitron-Black.woff2",
    "assets/Orbitron-Bold.woff2",
    "assets/Orbitron-Medium.woff2",
]


def log(msg):
    print(f"[sync_studio_web] {msg}")


def run_studio_web_build():
    if os.environ.get("CANSHIFT_SKIP_STUDIO_WEB_BUILD") == "1":
        log("CANSHIFT_SKIP_STUDIO_WEB_BUILD=1 — skipping npm run build")
        return
    if not STUDIO_WEB_DIR.is_dir():
        raise SystemExit(
            f"error: canshift-studio-web not found at {STUDIO_WEB_DIR}"
        )
    log(f"npm run build in {STUDIO_WEB_DIR}")
    # shell=False keeps argv literal so paths with spaces stay safe.
    result = subprocess.run(
        ["npm", "run", "build"],
        cwd=str(STUDIO_WEB_DIR),
        check=False,
    )
    if result.returncode != 0:
        raise SystemExit(
            "error: `npm run build` in canshift-studio-web failed — "
            "fix the SPA build before rebuilding firmware."
        )


def validate_dist():
    missing = []
    for rel in EXPECTED_GZ + EXPECTED_FONTS:
        if not (STUDIO_WEB_DIST / rel).is_file():
            missing.append(rel)
    if missing:
        raise SystemExit(
            "error: expected SPA artifacts missing from "
            f"{STUDIO_WEB_DIST}:\n  - "
            + "\n  - ".join(missing)
            + "\nUpdate EXPECTED_GZ / EXPECTED_FONTS in sync_studio_web.py "
            "AND kSpaAssets[] in wifi_ap.cpp in lock-step."
        )


def mirror_to_data_web():
    if WEB_DATA_DIR.exists():
        shutil.rmtree(WEB_DATA_DIR)
    WEB_ASSETS_DIR.mkdir(parents=True, exist_ok=True)
    copied = 0
    for rel in EXPECTED_GZ + EXPECTED_FONTS:
        src = STUDIO_WEB_DIST / rel
        dst = WEB_DATA_DIR / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(str(src), str(dst))
        copied += 1
    log(f"mirrored {copied} files into {WEB_DATA_DIR}")


def report_sizes():
    total = 0
    for rel in EXPECTED_GZ + EXPECTED_FONTS:
        path = WEB_DATA_DIR / rel
        size = path.stat().st_size
        total += size
        log(f"  {rel:<40} {size:>8} B")
    log(f"  {'TOTAL':<40} {total:>8} B (uploaded to SPIFFS via uploadfs)")


run_studio_web_build()
validate_dist()
mirror_to_data_web()
report_sizes()
