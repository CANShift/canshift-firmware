# build_rust.py — PlatformIO pre-build hook that compiles the firmware Rust
# crates and adds the resulting staticlibs to the linker inputs. Each crate
# is opt-in via a `USE_RUST_<NAME>=1` build_flag so CI (which doesn't carry
# the `esp` toolchain) keeps passing on the default envs.
#
# Why opt-in:
#   - CI runs without `espup` installed and would fail otherwise.
#   - Until device-side validation lands on every port, the C++ path stays
#     the default and the Rust path lives in a dedicated env.
#
# To exercise a Rust port locally:
#   pio run -e crowpanel_28_rust              # ota-hmac
#   pio run -e crowpanel_28_rust_signal_map   # signal-map
#   pio run -e crowpanel_28_rust_can_parser   # can-parser
#   pio run -e crowpanel_28_rust_usb_envelope # usb-envelope
#   pio run -e crowpanel_28_rust_config_loader # config-loader
#
# Adding a new crate is one entry in `CRATES` below.

import os
import shutil
import subprocess
import sys

Import("env")  # noqa: F821 — provided by PlatformIO script context

PROJECT_DIR = env["PROJECT_DIR"]  # noqa: F821
RUST_DIR = os.path.join(PROJECT_DIR, "rust")
TARGET_TRIPLE = "xtensa-esp32-none-elf"

# Catalog of Rust crates the hook knows how to build. Each entry maps a
# build_flag (e.g. `USE_RUST_OTA_HMAC=1`) to the manifest path and the
# expected staticlib name. The hook walks this list, builds whichever
# crates have their flag enabled, and adds each `.a` + its C header dir to
# the link / include path.
CRATES = (
    {
        "flag": "USE_RUST_OTA_HMAC=1",
        "manifest": os.path.join(RUST_DIR, "ota-hmac", "Cargo.toml"),
        "libfile": "libota_hmac.a",
        # ota-hmac keeps its header next to the crate; the script adds the
        # path to CPPPATH below. Newer crates ship the header under
        # canshift-firmware/include/ (always on -I) instead, so they leave
        # this key out — see signal-map for the pattern.
        "include": os.path.join(RUST_DIR, "ota-hmac", "include"),
    },
    {
        "flag": "USE_RUST_SIGNAL_MAP=1",
        "manifest": os.path.join(RUST_DIR, "signal-map", "Cargo.toml"),
        "libfile": "libsignal_map.a",
        # Header lives in canshift-firmware/include/signal_map_rs.h per
        # #1177 spec — it's already on the firmware -I path, so no
        # CPPPATH append needed.
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
)


def fail(msg):
    sys.stderr.write(f"[rust] ERROR: {msg}\n")
    sys.exit(1)


BUILD_FLAG_STRS = [str(f) for f in env["BUILD_FLAGS"]]  # noqa: F821
enabled_crates = [c for c in CRATES if any(c["flag"] in f for f in BUILD_FLAG_STRS)]

if not enabled_crates:
    print("[rust] no USE_RUST_* flags set — skipping Rust build")
    sys.exit(0)

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


OBJCOPY = find_objcopy()
WEAK_MEM_SYMS = ("memcpy", "memmove", "memset", "memcmp", "bcmp")


def build_one(crate):
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

    localize_cmd = [OBJCOPY]
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


for crate in enabled_crates:
    build_one(crate)
