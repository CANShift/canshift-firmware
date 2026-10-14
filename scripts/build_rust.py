# PlatformIO pre-build: each USE_RUST_<NAME>=1 build_flag opts in one crate;
# CI without `espup` keeps passing on the default envs.
import os
import shutil
import subprocess
import sys

Import("env")  # noqa: F821 — provided by PlatformIO script context

PROJECT_DIR = env["PROJECT_DIR"]  # noqa: F821
RUST_DIR = os.path.join(PROJECT_DIR, "rust")
MCU = env.BoardConfig().get("build.mcu", "esp32")  # noqa: F821
TARGET_TRIPLE = "xtensa-esp32s3-none-elf" if MCU == "esp32s3" else "xtensa-esp32-none-elf"

MANIFEST = os.path.join(RUST_DIR, "canshift-ffi", "Cargo.toml")
LIBFILE = "libcanshift_ffi.a"

CRATES = (
    ("USE_RUST_SIGNAL_MAP=1", "signal-map"),
    ("USE_RUST_CAN_PARSER=1", "can-parser"),
    ("USE_RUST_USB_ENVELOPE=1", "usb-envelope"),
    ("USE_RUST_CONFIG_LOADER=1", "config-loader"),
    ("USE_RUST_FORMAT_FLOAT=1", "format-float"),
    ("USE_RUST_ERROR_STORE=1", "error-store"),
    ("USE_RUST_ALERT_ENGINE=1", "alert-engine"),
    ("USE_RUST_TIMER_CORE=1", "timer-core"),
    ("USE_RUST_LAYOUT_GRID=1", "layout-grid"),
    ("USE_RUST_CONTROL_STATE=1", "control-state"),
    ("USE_RUST_UNIT_CONVERT=1", "unit-convert"),
    ("USE_RUST_WORKSHOP_MODE=1", "workshop-mode"),
    ("USE_RUST_OTA_SIGNATURE=1", "ota-signature"),
)


def fail(msg):
    sys.stderr.write(f"[rust] ERROR: {msg}\n")
    sys.exit(1)


BUILD_FLAG_STRS = [str(f) for f in env["BUILD_FLAGS"]]  # noqa: F821
enabled_features = [feature for flag, feature in CRATES if any(flag in f for f in BUILD_FLAG_STRS)]


def check_cargo():
    if shutil.which("cargo") is None:
        fail(
            "cargo not found in PATH — install Rust + espup first:\n"
            "    cargo install espup --locked\n"
            "    espup install\n"
            "    source ~/export-esp.sh"
        )

# Localize weak `memcpy / memmove / memset / memcmp / bcmp` so the static
# lib doesn't override ESP-IDF's IRAM-resident copies. Without this, the
# device crashes in `read_id_core` before flash cache is enabled (the
# linker picks Rust's flash-resident weak symbols over ESP-IDF's IRAM
# strong ones — exact symptom is EXCCAUSE 7 "Cache disabled but cached
# memory region accessed" in an infinite reboot loop). `compiler_builtins`
# still provides the mangled internal versions, so Rust's own slice copy
# / ptr::copy_nonoverlapping continue to work.
def find_objcopy():
    candidate = os.path.join(
        os.path.expanduser("~"),
        ".rustup/toolchains/esp/xtensa-esp-elf/esp-15.2.0_20250920/xtensa-esp-elf/bin/xtensa-esp32-elf-objcopy",
    )
    if os.path.exists(candidate):
        return candidate
    found = shutil.which("xtensa-esp32-elf-objcopy")
    if found:
        return found
    fail(
        "xtensa-esp32-elf-objcopy not found — re-run `espup install` or add "
        "the esp toolchain bin dir to PATH"
    )
    return None  # unreachable


WEAK_MEM_SYMS = (
    "memcpy",
    "memmove",
    "memset",
    "memcmp",
    "bcmp",
    "strlen",
    "fmodf",
    "fmod",
    "__udivdi3",
)


def build_umbrella(objcopy):
    print(f"[rust] building canshift-ffi with {', '.join(enabled_features)}")
    cmd = [
        "cargo",
        "build",
        "--release",
        "--manifest-path",
        MANIFEST,
        "--no-default-features",
        "--features",
        ",".join(enabled_features),
        "--target",
        TARGET_TRIPLE,
        "-Z",
        "build-std=core,alloc",
    ]
    res = subprocess.run(cmd, cwd=RUST_DIR)
    if res.returncode != 0:
        fail(f"cargo build failed for {LIBFILE} (exit {res.returncode})")

    lib_path = os.path.join(RUST_DIR, "target", TARGET_TRIPLE, "release", LIBFILE)
    if not os.path.exists(lib_path):
        fail(f"expected staticlib not found at {lib_path}")

    localize_cmd = [objcopy]
    for sym in WEAK_MEM_SYMS:
        localize_cmd.extend(["--localize-symbol", sym])
    localize_cmd.append(lib_path)
    res = subprocess.run(localize_cmd)
    if res.returncode != 0:
        fail(f"objcopy --localize-symbol returned {res.returncode} for {LIBFILE}")

    lib_size_kb = os.path.getsize(lib_path) // 1024
    print(f"[rust] {LIBFILE} OK ({lib_size_kb} KB), mem symbols localized")

    env.Append(LIBS=[File(lib_path)])  # noqa: F821


# No sys.exit here: exiting an extra_script kills the whole SCons process
# and PlatformIO reports SUCCESS without compiling anything (#1762).
if not enabled_features:
    print("[rust] no USE_RUST_* flags set — skipping Rust build")
else:
    parity = subprocess.run(
        [sys.executable, os.path.join(PROJECT_DIR, "scripts", "check_ffi_parity.py")],
        check=False,
    )
    if parity.returncode != 0:
        fail("FFI constants disagree between rust/ and include/ — see above")
    check_cargo()
    build_umbrella(find_objcopy())
