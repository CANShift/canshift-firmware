import os
import shutil
import subprocess
import sys

Import("env")  # noqa: F821

PROJECT_DIR = env["PROJECT_DIR"]  # noqa: F821
RUST_DIR = os.path.join(PROJECT_DIR, "rust")
MANIFEST = os.path.join(RUST_DIR, "canshift-ffi", "Cargo.toml")
LIBFILE = "libcanshift_ffi.a"

FEATURES = (
    "signal-map",
    "can-parser",
    "usb-envelope",
    "config-loader",
    "format-float",
    "error-store",
    "alert-engine",
    "timer-core",
    "layout-grid",
    "control-state",
    "unit-convert",
    "workshop-mode",
    "ota-signature",
    "ble-link",
)


def fail(msg):
    sys.stderr.write(f"[rust-native] ERROR: {msg}\n")
    sys.exit(1)


if shutil.which("cargo") is None:
    fail("cargo not found in PATH — install Rust first")

print("[rust-native] building canshift-ffi")
cmd = [
    "cargo",
    "build",
    "--release",
    "--locked",
    "--manifest-path",
    MANIFEST,
    "--features",
    ",".join(FEATURES),
]
res = subprocess.run(cmd, cwd=RUST_DIR)
if res.returncode != 0:
    fail(f"cargo build failed for {LIBFILE} (exit {res.returncode})")

lib_path = os.path.join(RUST_DIR, "target", "release", LIBFILE)
if not os.path.exists(lib_path):
    fail(f"expected staticlib not found at {lib_path}")

env.Append(LIBS=[File(lib_path)])  # noqa: F821
