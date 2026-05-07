# extra_targets.py — PlatformIO extra_scripts
# - Adds `pio run -t flash_sd` to copy sd_contents/ to a mounted SD card.
# - Injects APP_VERSION_STR from canshift-studio/package.json so the firmware
#   reports the same version as the studio release that bundles it (issue #37).

Import("env")
import json
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


# Inject the version. Quotes need to survive shell + compiler — use the
# escaped-quote form PlatformIO expects.
#
# CRITICAL: PlatformIO has two SCons envs. `env` flows to framework + lib_deps;
# `projenv` flows to project src/. We must append to both, otherwise
# src/boot/boot_sequence.cpp falls back to "0.0.0-unset" from app_config.h
# even though framework/library code sees the right macro (issue #233).
_studio_version = read_studio_version()
_version_define = ("APP_VERSION_STR", env.StringifyMacro(_studio_version))
env.Append(CPPDEFINES=[_version_define])

try:
    Import("projenv")
    projenv.Append(CPPDEFINES=[_version_define])
except Exception as exc:
    raise SystemExit(
        f"error: APP_VERSION_STR injection into projenv failed: {exc}"
    ) from exc

print(f"firmware version (from studio package.json): {_studio_version}")
