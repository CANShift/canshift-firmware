#!/usr/bin/env python3
"""Fail the build when a scalar shared across the Rust/C++ FFI boundary drifts.

The headers under include/*_rs.h are hand-written, so every constant that sizes
a shared struct or buffer exists twice. A mismatch is silent until it corrupts
memory, so it is gated here rather than trusted to a comment.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# (label, rust file, rust const, c++ file, c++ symbol)
PAIRS = (
    (
        "timer lap capacity",
        "rust/timer-core/src/lib.rs",
        "LAP_CAPACITY",
        "include/timer_core_rs.h",
        "TIMER_CORE_LAP_CAPACITY",
    ),
    (
        "expression token cap",
        "rust/can-parser/src/expr.rs",
        "MAX_TOKENS",
        "include/can_parser_rs.h",
        "CANSHIFT_EXPR_MAX_TOKENS",
    ),
    (
        "expression reference cap",
        "rust/can-parser/src/expr_ffi.rs",
        "MAX_REFS",
        "include/can_parser_rs.h",
        "CANSHIFT_EXPR_MAX_REFS",
    ),
    (
        "severity level count",
        "rust/alert-engine/src/lib.rs",
        "SEVERITY_LEVEL_COUNT",
        "include/alert_engine_rs.h",
        "ALERT_SEVERITY_LEVEL_COUNT",
    ),
    (
        "stale dash group cap",
        "rust/alert-engine/src/bus_silence.rs",
        "STALE_DASH_GROUPS_MAX",
        "include/alert_engine_rs.h",
        "ALERT_STALE_DASH_GROUPS_MAX",
    ),
    (
        "control state count",
        "rust/control-state/src/lib.rs",
        "CONTROL_STATE_COUNT",
        "include/control_state_rs.h",
        "CONTROL_STATE_COUNT",
    ),
    (
        "control step ceiling",
        "rust/control-state/src/lib.rs",
        "CONTROL_STEP_MAX",
        "include/control_state_rs.h",
        "CONTROL_STEP_MAX",
    ),
    (
        "control long-press hold",
        "rust/control-state/src/lib.rs",
        "CONTROL_LONG_PRESS_MS",
        "include/control_state_rs.h",
        "CONTROL_LONG_PRESS_MS",
    ),
    (
        "control splash change window",
        "rust/control-state/src/splash.rs",
        "CONTROL_SPLASH_CHANGE_MS",
        "include/control_state_rs.h",
        "CONTROL_SPLASH_CHANGE_MS",
    ),
    (
        "control splash refusal window",
        "rust/control-state/src/splash.rs",
        "CONTROL_SPLASH_REFUSAL_MS",
        "include/control_state_rs.h",
        "CONTROL_SPLASH_REFUSAL_MS",
    ),
    (
        "control splash kind count",
        "rust/control-state/src/splash.rs",
        "CONTROL_SPLASH_KIND_COUNT",
        "include/control_state_rs.h",
        "CONTROL_SPLASH_KIND_COUNT",
    ),
    (
        "CAN frame width",
        "rust/can-parser/src/lib.rs",
        "CAN_FRAME_MAX_BYTES",
        "include/app_config.h",
        "kCanFrameMaxBytes",
    ),
)

RUST_RE = "(?:pub(?:\\(crate\\))?\\s+)?const\\s+{name}\\s*:\\s*\\w+\\s*=\\s*(\\d+)"
CPP_RE = "(?:#define\\s+{name}\\s+|{name}\\s*=\\s*)(\\d+)"


def read(rel):
    with open(os.path.join(ROOT, rel), "r", encoding="utf-8") as handle:
        return handle.read()


def find(pattern, name, text, rel):
    match = re.search(pattern.format(name=re.escape(name)), text)
    if match is None:
        raise LookupError(f"{name} not found in {rel}")
    return int(match.group(1))


def main():
    failures = []
    for label, rust_rel, rust_name, cpp_rel, cpp_name in PAIRS:
        try:
            rust_value = find(RUST_RE, rust_name, read(rust_rel), rust_rel)
            cpp_value = find(CPP_RE, cpp_name, read(cpp_rel), cpp_rel)
        except (LookupError, OSError) as err:
            failures.append(f"{label}: {err}")
            continue
        if rust_value != cpp_value:
            failures.append(
                f"{label}: {rust_name}={rust_value} in {rust_rel} but "
                f"{cpp_name}={cpp_value} in {cpp_rel}"
            )

    if failures:
        sys.stderr.write("[ffi] shared constants disagree across the FFI boundary:\n")
        for line in failures:
            sys.stderr.write(f"[ffi]   {line}\n")
        return 1
    print(f"[ffi] {len(PAIRS)} shared constants agree")
    return 0


if __name__ == "__main__":
    sys.exit(main())
