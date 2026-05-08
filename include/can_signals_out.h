#pragma once
// can_signals_out.h — Outbound (firmware → ECU) CAN frame definitions
//
// UNVERIFIED: every constant in this file is a placeholder. The MaxxECU
// Street firmware does not document a stable map_switch input frame ID, so
// `CAN_OUT_MAP_SWITCH_ID` (0x600) is a safe-but-unverified guess that lives
// in the SAE J1939 proprietary range. Verify against the ECU's CAN input
// configuration before sending these frames on a live bus, otherwise the
// transmitted frame will simply be ignored — or, in the worst case, collide
// with another node that legitimately owns 0x600.
//
// Tracked in #262 follow-ups: make these IDs runtime-configurable from
// signals.json so users can override without recompiling.

#include <stdint.h>

// Map-switch frame: tells the ECU to load a specific tuning map slot.
// DLC=1, byte 0 = mapIndex (1-based, 1..8).
constexpr uint32_t CAN_OUT_MAP_SWITCH_ID = 0x600;
constexpr uint8_t CAN_OUT_MAP_SWITCH_DLC = 1;

// MaxxECU Street advertises 8 user map slots. mapIndex outside this range is
// rejected at dispatch time so the firmware never transmits a known-bad value.
constexpr uint8_t MAP_SWITCH_MIN_INDEX = 1;
constexpr uint8_t MAP_SWITCH_MAX_INDEX = 8;
