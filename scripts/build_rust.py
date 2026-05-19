# build_rust.py — PlatformIO pre-build hook that compiles the firmware Rust
# spike (issue #827 Phase 3) and adds the resulting staticlib to the linker
# inputs. Opt-in via `-DUSE_RUST_OTA_HMAC=1` in `[env:*]` build_flags.
#
# Why opt-in:
#   - CI runs without `espup` installed and would fail otherwise.
#   - Until device-side validation lands the C++ path stays the default.
#
# Flip the flag locally to exercise the Rust path:
#   pio run -e crowpanel_28 -t upload  (after editing platformio.ini)

import os
import shutil
import subprocess
import sys

Import("env")  # noqa: F821 — provided by PlatformIO script context

USE_RUST = any("USE_RUST_OTA_HMAC=1" in str(f) for f in env["BUILD_FLAGS"])  # noqa: F821

if not USE_RUST:
    print("[rust] USE_RUST_OTA_HMAC=0 — skipping Rust build")
    sys.exit(0)

PROJECT_DIR = env["PROJECT_DIR"]  # noqa: F821
RUST_DIR = os.path.join(PROJECT_DIR, "rust")
MANIFEST = os.path.join(RUST_DIR, "ota-hmac", "Cargo.toml")
TARGET_TRIPLE = "xtensa-esp32-none-elf"
LIB_PATH = os.path.join(
    RUST_DIR, "target", TARGET_TRIPLE, "release", "libota_hmac.a"
)
INCLUDE_DIR = os.path.join(RUST_DIR, "ota-hmac", "include")


def fail(msg):
    sys.stderr.write(f"[rust] ERROR: {msg}\n")
    sys.exit(1)


if shutil.which("cargo") is None:
    fail(
        "cargo not found in PATH — install Rust + espup first:\n"
        "    cargo install espup --locked\n"
        "    espup install\n"
        "    source ~/export-esp.sh"
    )

cmd = [
    "cargo",
    "build",
    "--release",
    "--manifest-path",
    MANIFEST,
    "--no-default-features",
    "--features",
    "ffi",
    "--target",
    TARGET_TRIPLE,
    "-Z",
    "build-std=core,alloc",
]

print(f"[rust] {' '.join(cmd)}")
res = subprocess.run(cmd, cwd=RUST_DIR)
if res.returncode != 0:
    fail(f"cargo build returned {res.returncode}")

if not os.path.exists(LIB_PATH):
    fail(f"expected staticlib not found at {LIB_PATH}")

lib_size_kb = os.path.getsize(LIB_PATH) // 1024
print(f"[rust] staticlib OK — {LIB_PATH} ({lib_size_kb} KB)")

# Add the staticlib to the linker inputs and expose the C header so
# `ota_hmac_bridge.cpp` can #include "ota_hmac_rs.h".
env.Append(LIBS=[File(LIB_PATH)])  # noqa: F821
env.Append(CPPPATH=[INCLUDE_DIR])  # noqa: F821
