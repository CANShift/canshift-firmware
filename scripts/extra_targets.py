# extra_targets.py — PlatformIO extra_scripts
# - Adds `pio run -t flash_sd` to copy sd_contents/ to a mounted SD card.
# - Injects APP_VERSION_STR from canshift-studio/package.json so the firmware
#   reports the same version as the studio release that bundles it (issue #37).
# - Injects CONFIG_SCHEMA_VERSION mirrored from canshift-core
#   (CURRENT_SCHEMA_VERSION in src/index.ts) so firmware and shared-core can
#   never disagree on the schema version (issue #203).

Import("env")
import json
import re
import subprocess
import sys
import os


def flash_sd(source, target, env):
    script = os.path.join(env["PROJECT_DIR"], "scripts", "flash_sd.py")
    result = subprocess.run([sys.executable, script], cwd=env["PROJECT_DIR"])
    if result.returncode != 0:
        print("\nTip: pass mount point manually:")
        print("  python3 scripts/flash_sd.py /Volumes/YOURSD")


env.AddCustomTarget(
    name="flash_sd",
    dependencies=None,
    actions=flash_sd,
    title="Flash SD card",
    description="Copy sd_contents/ (fonts + config) to mounted SD card",
)


def read_studio_version():
    """Single source of truth: canshift-studio/package.json `version`. The
    release workflow keys off the same file, so studio + firmware can never
    disagree as long as this script runs.

    Fails the build loudly on any read/parse error — a silent fallback
    macro (e.g. "0.0.0-unset") would brick the splash version with no way
    for the user to spot it on the device (issue #101)."""
    pkg = os.path.join(
        env["PROJECT_DIR"], "..", "canshift-studio", "package.json"
    )
    try:
        with open(pkg, "r") as fh:
            data = json.load(fh)
    except OSError as exc:
        raise SystemExit(f"error: cannot read {pkg}: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise SystemExit(f"error: bad JSON in {pkg}: {exc}") from exc

    version = data.get("version")
    if not isinstance(version, str) or not version:
        raise SystemExit(f"error: no 'version' field in {pkg}")
    return version


def read_core_schema_version():
    """Single source of truth: canshift-core/src/index.ts
    `CURRENT_SCHEMA_VERSION`. Mirrored into firmware so the C++ side and the
    TypeScript side cannot drift (issue #203).

    Fails the build loudly on any read/parse error — silently falling back to
    a stale literal would defeat the whole point of the alignment."""
    ts_path = os.path.join(
        env["PROJECT_DIR"], "..", "canshift-core", "src", "index.ts"
    )
    try:
        with open(ts_path, "r") as fh:
            source = fh.read()
    except OSError as exc:
        raise SystemExit(f"error: cannot read {ts_path}: {exc}") from exc

    # Match: export const CURRENT_SCHEMA_VERSION = '1.10.0' as const
    match = re.search(
        r"CURRENT_SCHEMA_VERSION\s*=\s*['\"]([^'\"]+)['\"]",
        source,
    )
    if not match:
        raise SystemExit(
            f"error: CURRENT_SCHEMA_VERSION literal not found in {ts_path}"
        )
    return match.group(1)


# Inject both versions. Quotes need to survive shell + compiler — use the
# escaped-quote form PlatformIO expects.
#
# CRITICAL: PlatformIO has two SCons envs. `env` flows to framework + lib_deps;
# `projenv` flows to project src/. We must append to both, otherwise
# src/boot/boot_sequence.cpp falls back to "0.0.0-unset" from app_config.h
# even though framework/library code sees the right macro (issue #233).
_studio_version = read_studio_version()
_schema_version = read_core_schema_version()
_defines = [
    ("APP_VERSION_STR", env.StringifyMacro(_studio_version)),
    ("CONFIG_SCHEMA_VERSION", env.StringifyMacro(_schema_version)),
]
env.Append(CPPDEFINES=_defines)

try:
    Import("projenv")
    projenv.Append(CPPDEFINES=_defines)
except Exception as exc:
    raise SystemExit(
        f"error: build-flag injection into projenv failed: {exc}"
    ) from exc

print(f"firmware version (from studio package.json): {_studio_version}")
print(f"config schema version (from canshift-core): {_schema_version}")
