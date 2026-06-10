#pragma once
// Boot-time fallback — runtime reads out.map_switch.{id,extended} from signals.json (#317).
// 0x600 is unverified — SAE J1939 proprietary range, verify against ECU.

#include <stdint.h>

// DLC=1, byte 0 = mapIndex (1-based, 1..8).
constexpr uint32_t CAN_OUT_MAP_SWITCH_ID = 0x600;
constexpr uint8_t CAN_OUT_MAP_SWITCH_DLC = 1;

constexpr uint8_t MAP_SWITCH_MIN_INDEX = 1;
constexpr uint8_t MAP_SWITCH_MAX_INDEX = 8;
