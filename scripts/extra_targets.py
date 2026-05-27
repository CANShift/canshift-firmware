# extra_targets.py — PlatformIO extra_scripts
# - Injects APP_VERSION_STR from canshift-firmware/package.json so the
#   firmware splash, BLE STATUS char, and /status endpoint report the same
#   version the release workflow tags (issue #37). The source-of-truth moved
#   here from canshift-studio/package.json once the Electron Studio package
#   was decommissioned — firmware is now the only artifact in releases.
# - Injects CONFIG_SCHEMA_VERSION mirrored from canshift-core
#   (CURRENT_SCHEMA_VERSION in src/index.ts) so firmware and shared-core can
#   never disagree on the schema version (issue #203).
# - Injects OTA_HMAC_SECRET from canshift-firmware/secrets.ini (gitignored)
#   so the OTA HMAC trailer can be verified at upload time. Hard-fails the
#   build on prod flavours when secrets.ini is missing or still holds the
#   placeholder string (issue #667). Dev flavours (env name contains "debug",
#   "sim", or "native") accept the placeholder with a loud WARN line.

Import("env")
import configparser
import json
import re
import os


def read_firmware_version():
    """Single source of truth: canshift-firmware/package.json `version`. The
    release workflow keys off the same file, so the release tag and the
    APP_VERSION_STR baked into the binary can never disagree as long as this
    script runs.

    Fails the build loudly on any read/parse error — a silent fallback
    macro (e.g. "0.0.0-unset") would brick the splash version with no way
    for the user to spot it on the device (issue #101)."""
    pkg = os.path.join(env["PROJECT_DIR"], "package.json")
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


# Placeholder secret literal from include/app_config.h fallback. Kept in sync
# manually — if you rename the placeholder, update both sites.
PLACEHOLDER_SECRET = "DEV_INSECURE_REPLACE_BEFORE_PROD"

# Example string shipped in secrets.ini.example. Treated as "not configured".
EXAMPLE_SECRET = "REPLACE_WITH_OUTPUT_OF_openssl_rand_hex_32"

# Build flavours that may use the placeholder secret. Issue #910 — moved from
# substring matching (`"debug" in "crowpanel_28_debug_perf"` was true) to an
# exact-match set. A maintainer accidentally tagging a release built from
# `crowpanel_28_debug_…` no longer slips a placeholder OTA secret into a
# production artifact. Add new dev envs explicitly — fail-closed wins over
# fail-open every time.
DEV_ENV_NAMES = frozenset(("sim", "debug", "native"))


def is_dev_build():
    """Return True iff the current PlatformIO env name marks a dev build.

    The discriminator is the env name (PIOENV) — simpler than threading a new
    APP_BUILD_FLAVOR env var through the build, and it matches the way the
    existing envs are already split (production = crowpanel_28 / debug-perf /
    secure; dev = sim / debug / native).

    GitHub Actions pull_request CI is NO LONGER auto-allowed (#910). The PR
    workflow must set OTA_HMAC_SECRET to the same value as the release
    workflow (or to a deliberate test value) — that way the PR build either
    produces a binary safe to flash, or fails loud. Auto-accepting the
    placeholder on PR CI meant a malicious PR could land a binary built with
    a known secret on every reviewer's machine."""
    pio_env = env.get("PIOENV", "") or ""
    return pio_env.lower() in DEV_ENV_NAMES


def read_ota_hmac_secret():
    """Read OTA_HMAC_SECRET from canshift-firmware/secrets.ini.

    Returns (secret, is_fallback). The caller decides whether a fallback is
    tolerable based on the current build flavour: dev builds accept the
    placeholder with a WARN, prod builds hard-fail (issue #667 — the
    placeholder must never reach a production binary silently).

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
        print(f"warning: cannot parse {ini_path}: {exc} — treating as missing")
        return None, True

    if not parser.has_option("ota", "hmac_secret"):
        return None, True

    secret = parser.get("ota", "hmac_secret").strip()
    if not secret:
        return None, True

    # The example value is not a real secret — surface it the same way as a
    # missing file so the prod gate fires.
    if secret == EXAMPLE_SECRET or secret == PLACEHOLDER_SECRET:
        return secret, True

    return secret, False


def enforce_ota_secret_policy(secret, is_fallback):
    """Gate the build on the OTA secret. Prod flavours must have a real
    secret in secrets.ini; dev flavours may keep the placeholder with a loud
    WARN. Issue #667."""
    if not is_fallback:
        return

    pio_env = env.get("PIOENV", "<unknown>")
    if is_dev_build():
        print(
            f"WARN: OTA HMAC secret falls back to the dev placeholder for "
            f"env '{pio_env}'. This build MUST NOT be shipped to users."
        )
        return

    raise SystemExit(
        "ERROR: refusing to build production firmware with the placeholder "
        "OTA HMAC secret (env '"
        + str(pio_env)
        + "'). Create canshift-firmware/secrets.ini with a fresh secret:\n"
        + "    openssl rand -hex 32\n"
        + "Then set it under [ota] hmac_secret = <hex>. To build a dev "
        + "image without a real secret, pick a dev env (sim / debug / "
        + "native). Closes #667."
    )


# Inject all three macros. Quotes need to survive shell + compiler — use the
# escaped-quote form PlatformIO expects.
#
# CRITICAL: PlatformIO has two SCons envs. `env` flows to framework + lib_deps;
# `projenv` flows to project src/. We must append to both, otherwise
# src/boot/boot_sequence.cpp falls back to "0.0.0-unset" from app_config.h
# even though framework/library code sees the right macro (issue #233).
_firmware_version = read_firmware_version()
_schema_version = read_core_schema_version()
_ota_secret, _ota_fallback = read_ota_hmac_secret()

# Hard-fail prod builds before we even hand defines to the compiler, so a
# misconfigured secrets.ini cannot produce a flashable binary that silently
# trusts the placeholder OTA key (issue #667).
enforce_ota_secret_policy(_ota_secret, _ota_fallback)

_defines = [
    ("APP_VERSION_STR", env.StringifyMacro(_firmware_version)),
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

print(f"firmware version (from canshift-firmware package.json): {_firmware_version}")
print(f"config schema version (from canshift-core): {_schema_version}")
if _ota_fallback:
    # Dev path only — prod would already have raised in
    # enforce_ota_secret_policy() above.
    print(
        "WARN: secrets.ini missing or incomplete — OTA_HMAC_SECRET falls back "
        "to the dev placeholder in app_config.h. This build MUST NOT ship."
    )
else:
    print("OTA HMAC secret loaded from secrets.ini")
