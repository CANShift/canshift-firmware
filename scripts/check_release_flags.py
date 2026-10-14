import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONFIG = os.path.join(ROOT, "platformio.ini")
DEV_ONLY_FLAGS = ("APP_OTA_ALLOW_UNSIGNED",)


def main():
    with open(CONFIG, "r", encoding="utf-8") as handle:
        text = handle.read()
    found = [flag for flag in DEV_ONLY_FLAGS if flag in text]
    if found:
        sys.stderr.write(
            f"[flags] {', '.join(found)} must never appear in platformio.ini; "
            "pass it through PLATFORMIO_BUILD_FLAGS on a local debug build\n"
        )
        return 1
    print(f"[flags] no env enables {', '.join(DEV_ONLY_FLAGS)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
