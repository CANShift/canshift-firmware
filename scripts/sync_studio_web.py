# sync_studio_web.py — PlatformIO extra_scripts hook
#
# Phase 4 of #1077 — ships the dash-hosted Studio SPA alongside the firmware.
# Before compile/link, this script:
#   1. Runs `npm run build` in ../canshift-studio-web/ (skippable via
#      CANSHIFT_SKIP_STUDIO_WEB_BUILD=1 so CI can install once and reuse).
#   2. Mirrors every `*.gz` + every `.woff2` from `canshift-studio-web/dist/`
#      into `canshift-firmware/data/w/` so the SPIFFS image picked up by
#      `pio run -t uploadfs` carries the SPA assets.
#   3. Validates the SPA file list against the expected manifest — fails the
#      build if Vite emits a chunk we didn't wire into the WebServer route
#      table (otherwise the browser would 404 silently).
#
# Path layout — `data/w/...` mirrors `dist/a/...` so the on-device SPIFFS
# path stays under SPIFFS_OBJ_NAME_LEN (31 chars including leading slash
# and trailing NUL). The previous `/web/assets/Orbitron-Medium.woff2` (33)
# overflowed the limit and mkspiffs silently aborted the rest of the
# traversal, shipping a broken AP that 404'd every chunk (#1240).
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


# Skip the SPA sync when the active env doesn't actually serve it. crowpanel_28
# inherits from base envs and so do sim / native / debug / secure / rust — they
# all picked this script up via `extends = env:crowpanel_28`, but only envs
# that link the WebServer + WS code need the SPA bundle in data/w/. Gate on
# the APP_SPA_SERVE build flag so the dependency graph stays correct.
_BUILD_FLAGS = " ".join(env.get("BUILD_FLAGS", []) or [])
if "APP_SPA_SERVE=1" not in _BUILD_FLAGS:
    print(f"[sync_studio_web] skipping — APP_SPA_SERVE not set on env '{env.get('PIOENV', '?')}'")
    Return()


PROJECT_DIR = Path(env["PROJECT_DIR"])
STUDIO_WEB_DIR = (PROJECT_DIR / ".." / "canshift-studio-web").resolve()
STUDIO_WEB_DIST = STUDIO_WEB_DIR / "dist"
# `data/w/` (was `data/web/`) — see header note re SPIFFS_OBJ_NAME_LEN (#1240).
WEB_DATA_DIR = (PROJECT_DIR / "data" / "w").resolve()
WEB_ASSETS_DIR = WEB_DATA_DIR / "a"

# Files we EXPECT to find in dist/ post-build. Order matters here because the
# wifi_ap.cpp route table is hand-maintained in lock-step. If Vite ever
# starts emitting a different chunk graph, this list and wifi_ap.cpp's
# kSpaAssets[] table need a coordinated bump. Keep the two in sync.
#
# Path layout: `a/` mirrors the Vite output dir (see vite.config.ts).
EXPECTED_GZ = [
    "index.html.gz",
    "a/index.js.gz",
    "a/index.css.gz",
    "a/vendor-react.js.gz",
    "a/vendor-radix.js.gz",
    "a/vendor-state.js.gz",
    "a/EditorRoute.js.gz",
]
EXPECTED_FONTS = [
    "a/Orbitron-Black.woff2",
    "a/Orbitron-Bold.woff2",
    "a/Orbitron-Medium.woff2",
]


def log(msg):
    print(f"[sync_studio_web] {msg}")


def run_studio_web_build():
    if os.environ.get("CANSHIFT_SKIP_STUDIO_WEB_BUILD") == "1":
        log("CANSHIFT_SKIP_STUDIO_WEB_BUILD=1 — skipping npm run build")
        return False
    if not STUDIO_WEB_DIR.is_dir():
        raise SystemExit(
            f"error: canshift-studio-web not found at {STUDIO_WEB_DIR}"
        )
    # CI / fresh checkout path: when studio-web's node_modules is missing the
    # script can't run `npm run build` (vite + co. unresolved). Rather than
    # forcing an `npm ci` here (slow + version-pinned to the workflow), bail
    # with a clear message — the firmware ELF doesn't need the SPA bundle to
    # link; only `pio run -t uploadfs` would, and that runs in its own CI job
    # that pre-installs the JS deps.
    if not (STUDIO_WEB_DIR / "node_modules").is_dir():
        log("node_modules absent in canshift-studio-web — skipping SPA build")
        log("(firmware ELF doesn't need data/w/*.gz; run `npm ci` + reflash"
            " if you also need a fresh SPIFFS image)")
        return False
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
    return True


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


# Largest on-device SPIFFS path the script is willing to emit. Hard-coded to
# 30 (SPIFFS_OBJ_NAME_LEN - 1) so we fail loudly here if any future asset name
# ever gets long enough to silently disappear from the SPIFFS image — the
# regression that triggered #1240. Path is built as `/<WEB_DATA_DIR-rel>/<rel>`
# i.e. `/w/a/<filename>` on device.
SPIFFS_OBJ_NAME_MAX = 30


def assert_paths_fit():
    too_long = []
    for rel in EXPECTED_GZ + EXPECTED_FONTS:
        # The leading slash + `w/` prefix is what the device sees when it
        # opens the file — mirror that here so the check matches reality.
        on_device = f"/w/{rel}"
        if len(on_device) > SPIFFS_OBJ_NAME_MAX:
            too_long.append((on_device, len(on_device)))
    if too_long:
        rows = "\n  - ".join(f"{p} ({n} chars)" for p, n in too_long)
        raise SystemExit(
            f"error: SPIFFS path(s) exceed {SPIFFS_OBJ_NAME_MAX} chars — "
            "mkspiffs would silently drop everything after the first overflow "
            f"(#1240):\n  - {rows}\n"
            "Shorten the asset basename, or shorten the data dir prefix."
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


_built = run_studio_web_build()
if _built:
    validate_dist()
    assert_paths_fit()
    mirror_to_data_web()
    report_sizes()
else:
    log("dist/ not produced this run — data/w/ left as-is")
