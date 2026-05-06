#pragma once
// rotation_config.h — NVS-backed rotation override for the display.
//
// The compile-time default lives in `HW_DISPLAY_ROTATION` (hardware_profile.h)
// and matches the expected mounting orientation of the board. This module
// layers an optional 180° flip on top of that default, so a user mounting the
// dashboard the other way around can flip the display via studio without
// rebuilding firmware.
//
// Only 0° and 180° are supported (90°/270° would change the aspect ratio and
// require new layouts — see #18).

#include <stdint.h>

namespace RotationConfig {

// Returns the persisted offset in degrees: either 0 or 180.
uint16_t getOffsetDeg();

// Returns the LovyanGFX rotation index (0–3) to apply at boot, combining the
// compile-time default and the persisted offset.
uint8_t computeLgfxRotation();

// Persist the offset (0 or 180), invalidate touch calibration so first-boot
// recalibration runs on next boot, and reboot the device. Never returns.
void applyAndReboot(uint16_t offsetDeg);

} // namespace RotationConfig
