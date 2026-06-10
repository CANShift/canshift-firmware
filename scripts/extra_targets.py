# PlatformIO extra_scripts — injects APP_VERSION_STR (#37), CONFIG_SCHEMA_VERSION
# from canshift-core/index.ts (#203), and OTA_HMAC_SECRET from secrets.ini (#667).
Import("env")
import configparser
import json
import re
import os


def read_firmware_version():
    """Source of truth: package.json. Fails loudly — silent fallback bricks
    the splash version with no way to spot it on device (#101)."""
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
    """Mirrors CURRENT_SCHEMA_VERSION from canshift-core/index.ts (#203).
    Fails loudly — a stale literal would defeat the alignment."""
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


# Must match the include/app_config.h fallback literal.
PLACEHOLDER_SECRET = "DEV_INSECURE_REPLACE_BEFORE_PROD"
EXAMPLE_SECRET = "REPLACE_WITH_OUTPUT_OF_openssl_rand_hex_32"

# Exact-match set — substring matching let `crowpanel_28_debug_perf` slip
# through as a dev build (#910).
DEV_ENV_NAMES = frozenset(("sim", "debug", "native"))


def is_dev_build():
    """PR CI must set OTA_HMAC_SECRET explicitly — auto-accepting placeholder
    let malicious PRs ship a known-secret binary to reviewers (#910)."""
    pio_env = env.get("PIOENV", "") or ""
    return pio_env.lower() in DEV_ENV_NAMES


def read_ota_hmac_secret():
    """Returns (secret, is_fallback). secrets.ini format: [ota] hmac_secret = ..."""
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

    if secret == EXAMPLE_SECRET or secret == PLACEHOLDER_SECRET:
        return secret, True

    return secret, False


def enforce_ota_secret_policy(secret, is_fallback):
    """Prod hard-fails on placeholder; dev WARNs (#667)."""
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


# PlatformIO has two SCons envs — `env` (framework + lib_deps) and `projenv`
# (project src/). Both need the defines or src/ falls back (#233).
_firmware_version = read_firmware_version()
_schema_version = read_core_schema_version()
_ota_secret, _ota_fallback = read_ota_hmac_secret()

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
