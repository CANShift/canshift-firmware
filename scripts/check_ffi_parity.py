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
        "display idle live",
        "rust/alert-engine/src/lib.rs",
        "DISPLAY_IDLE_LIVE",
        "include/alert_engine_rs.h",
        "kAlertDisplayIdleLive",
    ),
    (
        "display idle dim",
        "rust/alert-engine/src/lib.rs",
        "DISPLAY_IDLE_DIM",
        "include/alert_engine_rs.h",
        "kAlertDisplayIdleDim",
    ),
    (
        "display idle off",
        "rust/alert-engine/src/lib.rs",
        "DISPLAY_IDLE_OFF",
        "include/alert_engine_rs.h",
        "kAlertDisplayIdleOff",
    ),
    (
        "timer lap capacity",
        "rust/timer-core/src/lib.rs",
        "LAP_CAPACITY",
        "include/timer_core_rs.h",
        "kTimerCoreLapCapacity",
    ),
    (
        "expression token cap",
        "rust/can-parser/src/expr.rs",
        "MAX_TOKENS",
        "include/can_parser_rs.h",
        "kCanshiftExprMaxTokens",
    ),
    (
        "expression stack depth cap",
        "rust/can-parser/src/expr_compile.rs",
        "MAX_STACK_DEPTH",
        "include/can_parser_rs.h",
        "kCanshiftExprMaxStackDepth",
    ),
    (
        "expression lex error",
        "rust/can-parser/src/expr_ffi.rs",
        "COMPILE_ERR_LEX",
        "include/can_parser_rs.h",
        "kCanshiftExprErrLex",
    ),
    (
        "expression too-long error",
        "rust/can-parser/src/expr_ffi.rs",
        "COMPILE_ERR_TOO_LONG",
        "include/can_parser_rs.h",
        "kCanshiftExprErrTooLong",
    ),
    (
        "expression syntax error",
        "rust/can-parser/src/expr_ffi.rs",
        "COMPILE_ERR_SYNTAX",
        "include/can_parser_rs.h",
        "kCanshiftExprErrSyntax",
    ),
    (
        "expression too-deep error",
        "rust/can-parser/src/expr_ffi.rs",
        "COMPILE_ERR_TOO_DEEP",
        "include/can_parser_rs.h",
        "kCanshiftExprErrTooDeep",
    ),
    (
        "expression no-room error",
        "rust/can-parser/src/expr_ffi.rs",
        "COMPILE_ERR_NO_ROOM",
        "include/can_parser_rs.h",
        "kCanshiftExprErrNoRoom",
    ),
    (
        "expression reference cap",
        "rust/can-parser/src/expr_ffi.rs",
        "MAX_REFS",
        "include/can_parser_rs.h",
        "kCanshiftExprMaxRefs",
    ),
    (
        "severity level count",
        "rust/alert-engine/src/lib.rs",
        "SEVERITY_LEVEL_COUNT",
        "include/alert_engine_rs.h",
        "kAlertSeverityLevelCount",
    ),
    (
        "stale dash group cap",
        "rust/alert-engine/src/bus_silence.rs",
        "STALE_DASH_GROUPS_MAX",
        "include/alert_engine_rs.h",
        "kAlertStaleDashGroupsMax",
    ),
    (
        "control state count",
        "rust/control-state/src/lib.rs",
        "CONTROL_STATE_COUNT",
        "include/control_state_rs.h",
        "kControlStateCount",
    ),
    (
        "control step ceiling",
        "rust/control-state/src/lib.rs",
        "CONTROL_STEP_MAX",
        "include/control_state_rs.h",
        "kControlStepMax",
    ),
    (
        "control long-press hold",
        "rust/control-state/src/lib.rs",
        "CONTROL_LONG_PRESS_MS",
        "include/control_state_rs.h",
        "kControlLongPressMs",
    ),
    (
        "control splash change window",
        "rust/control-state/src/splash.rs",
        "CONTROL_SPLASH_CHANGE_MS",
        "include/control_state_rs.h",
        "kControlSplashChangeMs",
    ),
    (
        "control splash refusal window",
        "rust/control-state/src/splash.rs",
        "CONTROL_SPLASH_REFUSAL_MS",
        "include/control_state_rs.h",
        "kControlSplashRefusalMs",
    ),
    (
        "control splash kind count",
        "rust/control-state/src/splash.rs",
        "CONTROL_SPLASH_KIND_COUNT",
        "include/control_state_rs.h",
        "kControlSplashKindCount",
    ),
    (
        "cut kind count",
        "rust/alert-engine/src/cuts.rs",
        "CUT_KIND_COUNT",
        "include/alert_engine_rs.h",
        "kAlertCutKindCount",
    ),
    (
        "cut row capacity",
        "rust/alert-engine/src/cuts.rs",
        "CUT_ROW_CAPACITY",
        "include/alert_engine_rs.h",
        "kAlertCutRowCapacity",
    ),
    (
        "cut minimum visible ms",
        "rust/alert-engine/src/cuts.rs",
        "CUT_MIN_VISIBLE_MS",
        "include/alert_engine_rs.h",
        "kAlertCutMinVisibleMs",
    ),
    (
        "CAN frame width",
        "rust/can-parser/src/lib.rs",
        "CAN_FRAME_MAX_BYTES",
        "include/app_config.h",
        "kCanFrameMaxBytes",
    ),
    (
        "workshop entropy bytes",
        "rust/workshop-mode/src/credentials.rs",
        "WORKSHOP_ENTROPY_BYTES",
        "include/workshop_mode_rs.h",
        "kWorkshopEntropyBytes",
    ),
    (
        "workshop ssid cap",
        "rust/workshop-mode/src/credentials.rs",
        "WORKSHOP_SSID_CAP",
        "include/workshop_mode_rs.h",
        "kWorkshopSsidCap",
    ),
    (
        "workshop password cap",
        "rust/workshop-mode/src/credentials.rs",
        "WORKSHOP_PASSWORD_CAP",
        "include/workshop_mode_rs.h",
        "kWorkshopPasswordCap",
    ),
    (
        "workshop join payload cap",
        "rust/workshop-mode/src/join_payload.rs",
        "WORKSHOP_JOIN_PAYLOAD_CAP",
        "include/workshop_mode_rs.h",
        "kWorkshopJoinPayloadCap",
    ),
    (
        "workshop tuner path cap",
        "rust/workshop-mode/src/tuner_assets.rs",
        "WORKSHOP_TUNER_PATH_CAP",
        "include/workshop_mode_rs.h",
        "kWorkshopTunerPathCap",
    ),
    (
        "workshop state driving",
        "rust/workshop-mode/src/lib.rs",
        "WORKSHOP_STATE_DRIVING",
        "include/workshop_mode_rs.h",
        "kWorkshopStateDriving",
    ),
    (
        "workshop state entering",
        "rust/workshop-mode/src/lib.rs",
        "WORKSHOP_STATE_ENTERING",
        "include/workshop_mode_rs.h",
        "kWorkshopStateEntering",
    ),
    (
        "workshop state active",
        "rust/workshop-mode/src/lib.rs",
        "WORKSHOP_STATE_ACTIVE",
        "include/workshop_mode_rs.h",
        "kWorkshopStateActive",
    ),
    (
        "workshop event request",
        "rust/workshop-mode/src/lib.rs",
        "WORKSHOP_EVENT_REQUEST",
        "include/workshop_mode_rs.h",
        "kWorkshopEventRequest",
    ),
    (
        "workshop event entered",
        "rust/workshop-mode/src/lib.rs",
        "WORKSHOP_EVENT_ENTERED",
        "include/workshop_mode_rs.h",
        "kWorkshopEventEntered",
    ),
    (
        "workshop event failed",
        "rust/workshop-mode/src/lib.rs",
        "WORKSHOP_EVENT_FAILED",
        "include/workshop_mode_rs.h",
        "kWorkshopEventFailed",
    ),
    (
        "OTA signature bytes",
        "rust/ota-signature/src/lib.rs",
        "OTA_SIGNATURE_BYTES",
        "include/ota_signature_rs.h",
        "kOtaSignatureBytes",
    ),
    (
        "OTA signature state bytes",
        "rust/ota-signature/src/lib.rs",
        "OTA_SIGNATURE_STATE_BYTES",
        "include/ota_signature_rs.h",
        "kOtaSignatureStateBytes",
    ),
    (
        "OTA signature state align",
        "rust/ota-signature/src/lib.rs",
        "OTA_SIGNATURE_STATE_ALIGN",
        "include/ota_signature_rs.h",
        "kOtaSignatureStateAlign",
    ),
    (
        "unit system metric",
        "rust/unit-convert/src/lib.rs",
        "UNIT_SYSTEM_METRIC",
        "include/unit_convert_rs.h",
        "kUnitSystemMetric",
    ),
    (
        "unit system imperial",
        "rust/unit-convert/src/lib.rs",
        "UNIT_SYSTEM_IMPERIAL",
        "include/unit_convert_rs.h",
        "kUnitSystemImperial",
    ),
)

RUST_RE = "(?:pub(?:\\(crate\\))?\\s+)?const\\s+{name}\\s*:\\s*\\w+\\s*=\\s*(-?\\d+)"
CPP_RE = "(?:#define\\s+{name}\\s+|{name}\\s*=\\s*)(-?\\d+)"


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
