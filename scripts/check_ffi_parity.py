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
        "wheel button slot 1",
        "rust/signal-map/src/lib.rs",
        "WHEEL_BUTTON_1",
        "src/can/signal_map.h",
        "kWheelButton1",
    ),
    (
        "wheel button slot 2",
        "rust/signal-map/src/lib.rs",
        "WHEEL_BUTTON_2",
        "src/can/signal_map.h",
        "kWheelButton2",
    ),
    (
        "wheel button slot 3",
        "rust/signal-map/src/lib.rs",
        "WHEEL_BUTTON_3",
        "src/can/signal_map.h",
        "kWheelButton3",
    ),
    (
        "wheel button slot 4",
        "rust/signal-map/src/lib.rs",
        "WHEEL_BUTTON_4",
        "src/can/signal_map.h",
        "kWheelButton4",
    ),
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
        "touch read active period",
        "rust/alert-engine/src/display_power.rs",
        "TOUCH_READ_ACTIVE_MS",
        "include/alert_engine_rs.h",
        "kAlertTouchReadActiveMs",
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
        "page button debounce",
        "rust/control-state/src/page_button.rs",
        "PAGE_BUTTON_DEBOUNCE_MS",
        "include/control_state_rs.h",
        "kPageButtonDebounceMs",
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
        "critical no source",
        "rust/alert-engine/src/critical_ack.rs",
        "CRITICAL_NO_SOURCE",
        "include/alert_engine_rs.h",
        "kAlertCriticalNoSource",
    ),
    (
        "critical source capacity",
        "rust/alert-engine/src/critical_ack.rs",
        "CRITICAL_SOURCE_CAPACITY",
        "include/alert_engine_rs.h",
        "kAlertCriticalSourceCapacity",
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
        "workshop event resume",
        "rust/workshop-mode/src/lib.rs",
        "WORKSHOP_EVENT_RESUME",
        "include/workshop_mode_rs.h",
        "kWorkshopEventResume",
    ),
    (
        "workshop motion exit window",
        "rust/workshop-mode/src/motion.rs",
        "WORKSHOP_MOTION_EXIT_MS",
        "include/workshop_mode_rs.h",
        "kWorkshopMotionExitMs",
    ),
    (
        "workshop resume none",
        "rust/workshop-mode/src/lib.rs",
        "WORKSHOP_RESUME_NONE",
        "include/workshop_mode_rs.h",
        "kWorkshopResumeNone",
    ),
    (
        "workshop resume update",
        "rust/workshop-mode/src/lib.rs",
        "WORKSHOP_RESUME_UPDATE",
        "include/workshop_mode_rs.h",
        "kWorkshopResumeUpdate",
    ),
    (
        "workshop resume config",
        "rust/workshop-mode/src/lib.rs",
        "WORKSHOP_RESUME_CONFIG",
        "include/workshop_mode_rs.h",
        "kWorkshopResumeConfig",
    ),
    (
        "CAN bus state stopped",
        "rust/can-parser/src/bus_recovery.rs",
        "CAN_BUS_STATE_STOPPED",
        "include/can_parser_rs.h",
        "kCanBusStateStopped",
    ),
    (
        "CAN bus state running",
        "rust/can-parser/src/bus_recovery.rs",
        "CAN_BUS_STATE_RUNNING",
        "include/can_parser_rs.h",
        "kCanBusStateRunning",
    ),
    (
        "CAN bus state bus-off",
        "rust/can-parser/src/bus_recovery.rs",
        "CAN_BUS_STATE_BUS_OFF",
        "include/can_parser_rs.h",
        "kCanBusStateBusOff",
    ),
    (
        "CAN bus state recovering",
        "rust/can-parser/src/bus_recovery.rs",
        "CAN_BUS_STATE_RECOVERING",
        "include/can_parser_rs.h",
        "kCanBusStateRecovering",
    ),
    (
        "CAN bus action none",
        "rust/can-parser/src/bus_recovery.rs",
        "CAN_BUS_ACTION_NONE",
        "include/can_parser_rs.h",
        "kCanBusActionNone",
    ),
    (
        "CAN bus action recover",
        "rust/can-parser/src/bus_recovery.rs",
        "CAN_BUS_ACTION_RECOVER",
        "include/can_parser_rs.h",
        "kCanBusActionRecover",
    ),
    (
        "CAN bus action restart",
        "rust/can-parser/src/bus_recovery.rs",
        "CAN_BUS_ACTION_RESTART",
        "include/can_parser_rs.h",
        "kCanBusActionRestart",
    ),
    (
        "workshop socket busy window",
        "rust/workshop-mode/src/socket_admission.rs",
        "WORKSHOP_SOCKET_BUSY_WINDOW_MS",
        "include/workshop_mode_rs.h",
        "kWorkshopSocketBusyWindowMs",
    ),
    (
        "workshop admit adopt",
        "rust/workshop-mode/src/socket_admission.rs",
        "WORKSHOP_ADMIT_ADOPT",
        "include/workshop_mode_rs.h",
        "kWorkshopAdmitAdopt",
    ),
    (
        "workshop admit replace stale",
        "rust/workshop-mode/src/socket_admission.rs",
        "WORKSHOP_ADMIT_REPLACE_STALE",
        "include/workshop_mode_rs.h",
        "kWorkshopAdmitReplaceStale",
    ),
    (
        "workshop admit refuse busy",
        "rust/workshop-mode/src/socket_admission.rs",
        "WORKSHOP_ADMIT_REFUSE_BUSY",
        "include/workshop_mode_rs.h",
        "kWorkshopAdmitRefuseBusy",
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
    (
        "bus scan rate count",
        "rust/bus-scan/src/scan.rs",
        "BUS_SCAN_RATE_COUNT",
        "include/bus_scan_rs.h",
        "kBusScanRateCount",
    ),
    (
        "bus scan id capacity",
        "rust/bus-scan/src/seen_ids.rs",
        "BUS_SCAN_ID_CAPACITY",
        "include/bus_scan_rs.h",
        "kBusScanIdCapacity",
    ),
    (
        "bus scan collect window",
        "rust/bus-scan/src/scan.rs",
        "BUS_SCAN_COLLECT_MS",
        "include/bus_scan_rs.h",
        "kBusScanCollectMs",
    ),
    (
        "bus scan phase idle",
        "rust/bus-scan/src/scan.rs",
        "BUS_SCAN_PHASE_IDLE",
        "include/bus_scan_rs.h",
        "kBusScanPhaseIdle",
    ),
    (
        "bus scan phase probing",
        "rust/bus-scan/src/scan.rs",
        "BUS_SCAN_PHASE_PROBING",
        "include/bus_scan_rs.h",
        "kBusScanPhaseProbing",
    ),
    (
        "bus scan phase collecting",
        "rust/bus-scan/src/scan.rs",
        "BUS_SCAN_PHASE_COLLECTING",
        "include/bus_scan_rs.h",
        "kBusScanPhaseCollecting",
    ),
    (
        "bus scan phase done",
        "rust/bus-scan/src/scan.rs",
        "BUS_SCAN_PHASE_DONE",
        "include/bus_scan_rs.h",
        "kBusScanPhaseDone",
    ),
    (
        "bus scan outcome none",
        "rust/bus-scan/src/scan.rs",
        "BUS_SCAN_OUTCOME_NONE",
        "include/bus_scan_rs.h",
        "kBusScanOutcomeNone",
    ),
    (
        "bus scan outcome detected",
        "rust/bus-scan/src/scan.rs",
        "BUS_SCAN_OUTCOME_DETECTED",
        "include/bus_scan_rs.h",
        "kBusScanOutcomeDetected",
    ),
    (
        "bus scan outcome silent",
        "rust/bus-scan/src/scan.rs",
        "BUS_SCAN_OUTCOME_SILENT",
        "include/bus_scan_rs.h",
        "kBusScanOutcomeSilent",
    ),
    (
        "bus scan outcome unclear",
        "rust/bus-scan/src/scan.rs",
        "BUS_SCAN_OUTCOME_UNCLEAR",
        "include/bus_scan_rs.h",
        "kBusScanOutcomeUnclear",
    ),
    (
        "bus scan outcome failed",
        "rust/bus-scan/src/scan.rs",
        "BUS_SCAN_OUTCOME_FAILED",
        "include/bus_scan_rs.h",
        "kBusScanOutcomeFailed",
    ),
    (
        "bus scan action none",
        "rust/bus-scan/src/scan.rs",
        "BUS_SCAN_ACTION_NONE",
        "include/bus_scan_rs.h",
        "kBusScanActionNone",
    ),
    (
        "bus scan action install",
        "rust/bus-scan/src/scan.rs",
        "BUS_SCAN_ACTION_INSTALL",
        "include/bus_scan_rs.h",
        "kBusScanActionInstall",
    ),
    (
        "bus scan action restore",
        "rust/bus-scan/src/scan.rs",
        "BUS_SCAN_ACTION_RESTORE",
        "include/bus_scan_rs.h",
        "kBusScanActionRestore",
    ),
    (
        "tone sample rate",
        "rust/tone-sequencer/src/synth.rs",
        "TONE_SAMPLE_RATE_HZ",
        "include/tone_sequencer_rs.h",
        "kToneSampleRateHz",
    ),
    (
        "tone pattern none",
        "rust/tone-sequencer/src/pattern.rs",
        "TONE_PATTERN_NONE",
        "include/tone_sequencer_rs.h",
        "kTonePatternNone",
    ),
    (
        "tone pattern critical",
        "rust/tone-sequencer/src/pattern.rs",
        "TONE_PATTERN_CRITICAL",
        "include/tone_sequencer_rs.h",
        "kTonePatternCritical",
    ),
    (
        "tone pattern boot",
        "rust/tone-sequencer/src/pattern.rs",
        "TONE_PATTERN_BOOT",
        "include/tone_sequencer_rs.h",
        "kTonePatternBoot",
    ),
    (
        "tone pattern shift",
        "rust/tone-sequencer/src/pattern.rs",
        "TONE_PATTERN_SHIFT",
        "include/tone_sequencer_rs.h",
        "kTonePatternShift",
    ),
    (
        "tone volume max",
        "rust/tone-sequencer/src/volume.rs",
        "TONE_VOLUME_MAX_PCT",
        "include/tone_sequencer_rs.h",
        "kToneVolumeMaxPct",
    ),
    (
        "tone pattern count",
        "rust/tone-sequencer/src/pattern.rs",
        "TONE_PATTERN_COUNT",
        "include/tone_sequencer_rs.h",
        "kTonePatternCount",
    ),
    (
        "imu sector count",
        "rust/imu-core/src/envelope.rs",
        "SECTOR_COUNT",
        "include/imu_core_rs.h",
        "kImuSectorCount",
    ),
    (
        "imu level samples",
        "rust/imu-core/src/level.rs",
        "LEVEL_SAMPLES",
        "include/imu_core_rs.h",
        "kImuLevelSamples",
    ),
    (
        "imu level pending",
        "rust/imu-core/src/level.rs",
        "LEVEL_PENDING",
        "include/imu_core_rs.h",
        "kImuLevelPending",
    ),
    (
        "imu level ok",
        "rust/imu-core/src/level.rs",
        "LEVEL_OK",
        "include/imu_core_rs.h",
        "kImuLevelOk",
    ),
    (
        "imu level moving",
        "rust/imu-core/src/level.rs",
        "LEVEL_MOVING",
        "include/imu_core_rs.h",
        "kImuLevelMoving",
    ),
    (
        "imu level not one g",
        "rust/imu-core/src/level.rs",
        "LEVEL_NOT_ONE_G",
        "include/imu_core_rs.h",
        "kImuLevelNotOneG",
    ),
    (
        "imu level screen flat",
        "rust/imu-core/src/level.rs",
        "LEVEL_SCREEN_FLAT",
        "include/imu_core_rs.h",
        "kImuLevelScreenFlat",
    ),
    (
        "imu axis +x",
        "rust/imu-core/src/frame.rs",
        "IMU_AXIS_POS_X",
        "include/board_profile.h",
        "PosX",
    ),
    (
        "imu axis -x",
        "rust/imu-core/src/frame.rs",
        "IMU_AXIS_NEG_X",
        "include/board_profile.h",
        "NegX",
    ),
    (
        "imu axis +y",
        "rust/imu-core/src/frame.rs",
        "IMU_AXIS_POS_Y",
        "include/board_profile.h",
        "PosY",
    ),
    (
        "imu axis -y",
        "rust/imu-core/src/frame.rs",
        "IMU_AXIS_NEG_Y",
        "include/board_profile.h",
        "NegY",
    ),
    (
        "imu axis +z",
        "rust/imu-core/src/frame.rs",
        "IMU_AXIS_POS_Z",
        "include/board_profile.h",
        "PosZ",
    ),
    (
        "imu axis -z",
        "rust/imu-core/src/frame.rs",
        "IMU_AXIS_NEG_Z",
        "include/board_profile.h",
        "NegZ",
    ),
    (
        "launch idle",
        "rust/imu-core/src/launch.rs",
        "LAUNCH_IDLE",
        "include/imu_core_rs.h",
        "kLaunchIdle",
    ),
    (
        "launch armed",
        "rust/imu-core/src/launch.rs",
        "LAUNCH_ARMED",
        "include/imu_core_rs.h",
        "kLaunchArmed",
    ),
    (
        "launch running",
        "rust/imu-core/src/launch.rs",
        "LAUNCH_RUNNING",
        "include/imu_core_rs.h",
        "kLaunchRunning",
    ),
    (
        "launch done",
        "rust/imu-core/src/launch.rs",
        "LAUNCH_DONE",
        "include/imu_core_rs.h",
        "kLaunchDone",
    ),
    (
        "datalog signal slots",
        "rust/datalog/src/record.rs",
        "DATALOG_SIGNAL_SLOTS",
        "include/datalog_rs.h",
        "kDatalogSignalSlots",
    ),
    (
        "datalog max sample bytes",
        "rust/datalog/src/record.rs",
        "DATALOG_MAX_SAMPLE_BYTES",
        "include/datalog_rs.h",
        "kDatalogMaxSampleBytes",
    ),
    (
        "datalog max header bytes",
        "rust/datalog/src/record.rs",
        "DATALOG_MAX_HEADER_BYTES",
        "include/datalog_rs.h",
        "kDatalogMaxHeaderBytes",
    ),
    (
        "datalog max marker bytes",
        "rust/datalog/src/record.rs",
        "DATALOG_MAX_MARKER_BYTES",
        "include/datalog_rs.h",
        "kDatalogMaxMarkerBytes",
    ),
    (
        "datalog preamble bytes",
        "rust/datalog/src/record.rs",
        "DATALOG_PREAMBLE_BYTES",
        "include/datalog_rs.h",
        "kDatalogPreambleBytes",
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
