# PlatformIO extra_scripts — injects APP_VERSION_STR (#37) and
# CONFIG_SCHEMA_VERSION from canshift-core/index.ts (#203).
Import("env")
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
    """Mirrors CURRENT_SCHEMA_VERSION from a sibling canshift-core checkout when
    present (#203) — searched in src/schema-version.ts (its home since core#30)
    then src/index.ts. If neither yields the literal (no checkout, or the
    constant moved again), falls back to the committed core-schema-version.txt
    pin with a warning instead of hard-erroring: a moved constant should degrade,
    not block builds (#52). The pin is kept in sync by the cross-repo parity job."""
    core_src = os.path.join(env["PROJECT_DIR"], "..", "canshift-core", "src")
    pattern = re.compile(r"CURRENT_SCHEMA_VERSION\s*=\s*['\"]([^'\"]+)['\"]")
    for name in ("schema-version.ts", "index.ts"):
        try:
            with open(os.path.join(core_src, name), "r") as fh:
                source = fh.read()
        except OSError:
            continue
        match = pattern.search(source)
        if match:
            return match.group(1)

    pin_path = os.path.join(env["PROJECT_DIR"], "core-schema-version.txt")
    try:
        with open(pin_path, "r") as fh:
            version = fh.read().strip()
    except OSError as exc:
        raise SystemExit(
            f"error: no CURRENT_SCHEMA_VERSION in sibling canshift-core and "
            f"cannot read {pin_path}: {exc}"
        ) from exc
    if not version:
        raise SystemExit(f"error: {pin_path} is empty")
    if os.path.isdir(os.path.join(env["PROJECT_DIR"], "..", "canshift-core")):
        print(
            f"WARN: CURRENT_SCHEMA_VERSION not found in sibling "
            f"canshift-core/src (schema-version.ts / index.ts) — using "
            f"core-schema-version.txt pin ({version})"
        )
    return version


# PlatformIO has two SCons envs — `env` (framework + lib_deps) and `projenv`
# (project src/). Both need the defines or src/ falls back (#233).
_firmware_version = read_firmware_version()
_schema_version = read_core_schema_version()

_defines = [
    ("APP_VERSION_STR", env.StringifyMacro(_firmware_version)),
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

print(f"firmware version (from canshift-firmware package.json): {_firmware_version}")
print(f"config schema version (from canshift-core): {_schema_version}")
