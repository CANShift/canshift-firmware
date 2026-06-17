import os
import shutil
import subprocess
import sys

Import("env")  # noqa: F821

PROJECT_DIR = env["PROJECT_DIR"]  # noqa: F821
RUST_DIR = os.path.join(PROJECT_DIR, "rust")

CRATES = (
    ("signal-map", "libsignal_map.a"),
    ("can-parser", "libcan_parser.a"),
    ("usb-envelope", "libusb_envelope.a"),
    ("config-loader", "libconfig_loader.a"),
    ("format-float", "libformat_float.a"),
    ("error-store", "liberror_store.a"),
    ("alert-engine", "libalert_engine.a"),
    ("sensor-color-ramp", "libsensor_color_ramp.a"),
    ("ota-hmac", "libota_hmac.a"),
)


def fail(msg):
    sys.stderr.write(f"[rust-native] ERROR: {msg}\n")
    sys.exit(1)


if shutil.which("cargo") is None:
    fail("cargo not found in PATH — install Rust first")

for crate, libfile in CRATES:
    manifest = os.path.join(RUST_DIR, crate, "Cargo.toml")
    print(f"[rust-native] building {crate}")
    cmd = [
        "cargo",
        "build",
        "--release",
        "--manifest-path",
        manifest,
        "--features",
        "ffi",
    ]
    res = subprocess.run(cmd, cwd=RUST_DIR)
    if res.returncode != 0:
        fail(f"cargo build failed for {libfile} (exit {res.returncode})")

    lib_path = os.path.join(RUST_DIR, "target", "release", libfile)
    if not os.path.exists(lib_path):
        fail(f"expected staticlib not found at {lib_path}")

    env.Append(LIBS=[File(lib_path)])  # noqa: F821
