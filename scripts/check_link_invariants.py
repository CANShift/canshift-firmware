import re
import sys

APP_OBJECT = "src/boot/boot_sequence.cpp.o"
RUST_TARGET = "rust/target"
LIBC_SYMBOLS = ("strlen", "fmodf", "fmod", "__udivdi3")


def section_owner(text, symbol):
    pattern = re.compile(
        r"^ \.(?:text|literal)\." + re.escape(symbol) + r"\s*\n?\s+0x([0-9a-f]+)\s+0x[0-9a-f]+\s+(\S+)",
        re.M,
    )
    owners = [obj for addr, obj in pattern.findall(text) if int(addr, 16) != 0]
    return owners


def check_rollback_override(text):
    owners = section_owner(text, "verifyRollbackLater")
    if not any(obj.endswith(APP_OBJECT) for obj in owners):
        return ["verifyRollbackLater is not the firmware's override: OTA rollback is cancelled before setup()"]
    return []


def check_libc_symbols(text):
    failures = []
    for symbol in LIBC_SYMBOLS:
        rust = [obj for obj in section_owner(text, symbol) if RUST_TARGET in obj]
        if rust:
            failures.append(f"{symbol} resolves to a Rust compiler_builtins object: {rust[0]}")
    return failures


def main():
    if len(sys.argv) != 2:
        sys.stderr.write("usage: check_link_invariants.py <firmware.map>\n")
        return 2
    with open(sys.argv[1], "r", encoding="utf-8", errors="replace") as handle:
        text = handle.read()
    failures = check_rollback_override(text) + check_libc_symbols(text)
    for failure in failures:
        sys.stderr.write(f"[link] {failure}\n")
    if failures:
        return 1
    print("[link] rollback override linked, libc symbols not taken from Rust")
    return 0


if __name__ == "__main__":
    sys.exit(main())
