# extra_targets.py — PlatformIO extra_scripts
# - Injects APP_VERSION_STR from canshift-studio/package.json so the firmware
#   reports the same version as the studio release that bundles it (issue #37).
# - Injects CONFIG_SCHEMA_VERSION mirrored from canshift-core
#   (CURRENT_SCHEMA_VERSION in src/index.ts) so firmware and shared-core can
#   never disagree on the schema version (issue #203).
# - Injects OTA_HMAC_SECRET from canshift-firmware/secrets.ini (gitignored)
#   so the OTA HMAC trailer can be verified at upload time. Falls back to a
#   placeholder with a loud warning if secrets.ini is missing (issue #205).

Import("env")
import configparser
import json
import re
import os


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


def read_ota_hmac_secret():
    """Read OTA_HMAC_SECRET from canshift-firmware/secrets.ini if present.

    Returns (secret, is_fallback). When secrets.ini is missing or the section
    is incomplete, falls back to the dev placeholder so the build still
    succeeds — but prints a loud warning so it can never go unnoticed.

    secrets.ini format:
        [ota]
        hmac_secret = <hex or ascii string, no quotes>
    """
    ini_path = os.path.join(env["PROJECT_DIR"], "secrets.ini")
    if not os.path.isfile(ini_path):
        return None, True

    parser = configparser.ConfigParser()
    try:
        parser.read(ini_path)
    except configparser.Error as exc:
        print(f"warning: cannot parse {ini_path}: {exc} — using fallback secret")
        return None, True

    if not parser.has_option("ota", "hmac_secret"):
        return None, True

    secret = parser.get("ota", "hmac_secret").strip()
    if not secret:
        return None, True
    return secret, False


# Inject all three macros. Quotes need to survive shell + compiler — use the
# escaped-quote form PlatformIO expects.
#
# CRITICAL: PlatformIO has two SCons envs. `env` flows to framework + lib_deps;
# `projenv` flows to project src/. We must append to both, otherwise
# src/boot/boot_sequence.cpp falls back to "0.0.0-unset" from app_config.h
# even though framework/library code sees the right macro (issue #233).
_studio_version = read_studio_version()
_schema_version = read_core_schema_version()
_ota_secret, _ota_fallback = read_ota_hmac_secret()

_defines = [
    ("APP_VERSION_STR", env.StringifyMacro(_studio_version)),
    ("CONFIG_SCHEMA_VERSION", env.StringifyMacro(_schema_version)),
]
if _ota_secret is not None:
    _defines.append(("OTA_HMAC_SECRET", env.StringifyMacro(_ota_secret)))

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
if _ota_fallback:
    print(
        "warning: secrets.ini missing or incomplete — OTA_HMAC_SECRET falls back "
        "to the placeholder in app_config.h. DO NOT ship this build to users."
    )
else:
    print("OTA HMAC secret loaded from secrets.ini")
