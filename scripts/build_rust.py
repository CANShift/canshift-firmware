# PlatformIO pre-build: each USE_RUST_<NAME>=1 build_flag opts in one crate;
# CI without `espup` keeps passing on the default envs.
import os
import shutil
import subprocess
import sys

Import("env")  # noqa: F821 — provided by PlatformIO script context

PROJECT_DIR = env["PROJECT_DIR"]  # noqa: F821
RUST_DIR = os.path.join(PROJECT_DIR, "rust")
TARGET_TRIPLE = "xtensa-esp32-none-elf"

# `include=None` means the header lives under canshift-firmware/include/
# (already on the -I path). ota-hmac is the legacy in-crate header path.
CRATES = (
    {
        "flag": "USE_RUST_OTA_HMAC=1",
        "manifest": os.path.join(RUST_DIR, "ota-hmac", "Cargo.toml"),
        "libfile": "libota_hmac.a",
        "include": os.path.join(RUST_DIR, "ota-hmac", "include"),
    },
    {
        "flag": "USE_RUST_SIGNAL_MAP=1",
        "manifest": os.path.join(RUST_DIR, "signal-map", "Cargo.toml"),
        "libfile": "libsignal_map.a",
        "include": None,
    },
    {
        "flag": "USE_RUST_CAN_PARSER=1",
        "manifest": os.path.join(RUST_DIR, "can-parser", "Cargo.toml"),
        "libfile": "libcan_parser.a",
        # Header at canshift-firmware/include/can_parser_rs.h, same
        # convention as signal-map.
        "include": None,
    },
    {
        "flag": "USE_RUST_USB_ENVELOPE=1",
        "manifest": os.path.join(RUST_DIR, "usb-envelope", "Cargo.toml"),
        "libfile": "libusb_envelope.a",
        # Header at canshift-firmware/include/usb_envelope_rs.h.
        "include": None,
    },
    {
        "flag": "USE_RUST_CONFIG_LOADER=1",
        "manifest": os.path.join(RUST_DIR, "config-loader", "Cargo.toml"),
        "libfile": "libconfig_loader.a",
        # Header at canshift-firmware/include/config_loader_rs.h.
        "include": None,
    },
    {
        "flag": "USE_RUST_FORMAT_FLOAT=1",
        "manifest": os.path.join(RUST_DIR, "format-float", "Cargo.toml"),
        "libfile": "libformat_float.a",
        # Header at canshift-firmware/include/format_float_rs.h.
        "include": None,
    },
    {
        "flag": "USE_RUST_ERROR_STORE=1",
        "manifest": os.path.join(RUST_DIR, "error-store", "Cargo.toml"),
        "libfile": "liberror_store.a",
        # Header at canshift-firmware/include/error_store_rs.h.
        "include": None,
    },
    {
        "flag": "USE_RUST_ALERT_ENGINE=1",
        "manifest": os.path.join(RUST_DIR, "alert-engine", "Cargo.toml"),
        "libfile": "libalert_engine.a",
        # Header at canshift-firmware/include/alert_engine_rs.h.
        "include": None,
    },
    {
        "flag": "USE_RUST_SENSOR_COLOR_RAMP=1",
        "manifest": os.path.join(RUST_DIR, "sensor-color-ramp", "Cargo.toml"),
        "libfile": "libsensor_color_ramp.a",
        # Header at canshift-firmware/include/sensor_color_ramp_rs.h.
        "include": None,
    },
    {
        "flag": "USE_RUST_TIMER_CORE=1",
        "manifest": os.path.join(RUST_DIR, "timer-core", "Cargo.toml"),
        "libfile": "libtimer_core.a",
        # Header at canshift-firmware/include/timer_core_rs.h.
        "include": None,
    },
    {
        "flag": "USE_RUST_LAYOUT_GRID=1",
        "manifest": os.path.join(RUST_DIR, "layout-grid", "Cargo.toml"),
        "libfile": "liblayout_grid.a",
        # Header at canshift-firmware/include/layout_grid_rs.h.
        "include": None,
    },
)


def fail(msg):
    sys.stderr.write(f"[rust] ERROR: {msg}\n")
    sys.exit(1)


BUILD_FLAG_STRS = [str(f) for f in env["BUILD_FLAGS"]]  # noqa: F821
enabled_crates = [c for c in CRATES if any(c["flag"] in f for f in BUILD_FLAG_STRS)]


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


WEAK_MEM_SYMS = ("memcpy", "memmove", "memset", "memcmp", "bcmp")


def build_one(crate, objcopy):
    print(f"[rust] building {os.path.relpath(crate['manifest'], PROJECT_DIR)}")
    cmd = [
        "cargo",
        "build",
        "--release",
        "--manifest-path",
        crate["manifest"],
        "--no-default-features",
        "--features",
        "ffi",
        "--target",
        TARGET_TRIPLE,
        "-Z",
        "build-std=core,alloc",
        # `build-std-features = []` in rust/.cargo/config.toml drops the
        # default `compiler-builtins-mem` so the staticlib doesn't expose
        # weak C-ABI memcpy / memmove / memset / memcmp — those collide
        # with ESP-IDF's IRAM-resident versions and crash the device
        # before flash cache is enabled.
    ]
    res = subprocess.run(cmd, cwd=RUST_DIR)
    if res.returncode != 0:
        fail(f"cargo build failed for {crate['libfile']} (exit {res.returncode})")

    lib_path = os.path.join(RUST_DIR, "target", TARGET_TRIPLE, "release", crate["libfile"])
    if not os.path.exists(lib_path):
        fail(f"expected staticlib not found at {lib_path}")

    localize_cmd = [objcopy]
    for sym in WEAK_MEM_SYMS:
        localize_cmd.extend(["--localize-symbol", sym])
    localize_cmd.append(lib_path)
    res = subprocess.run(localize_cmd)
    if res.returncode != 0:
        fail(f"objcopy --localize-symbol returned {res.returncode} for {crate['libfile']}")

    lib_size_kb = os.path.getsize(lib_path) // 1024
    print(f"[rust] {crate['libfile']} OK ({lib_size_kb} KB), mem symbols localized")

    env.Append(LIBS=[File(lib_path)])  # noqa: F821
    if crate.get("include") is not None:
        env.Append(CPPPATH=[crate["include"]])  # noqa: F821


# No sys.exit here: exiting an extra_script kills the whole SCons process
# and PlatformIO reports SUCCESS without compiling anything (#1762).
if not enabled_crates:
    print("[rust] no USE_RUST_* flags set — skipping Rust build")
else:
    check_cargo()
    objcopy = find_objcopy()
    for crate in enabled_crates:
        build_one(crate, objcopy)
