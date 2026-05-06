// rotation_config.cpp — NVS-backed rotation override implementation.

#include "rotation_config.h"
#include "hardware_profile.h"
#include "diag/logger.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>

static constexpr char NVS_NS[] = "display_cfg";
static constexpr char NVS_KEY_OFFSET[] = "rot_offset"; // uint16, degrees (0 or 180)

static constexpr char TOUCH_NVS_NS[] = "touch";
static constexpr char TOUCH_NVS_KEY_CAL[] = "cal";

namespace RotationConfig {

uint16_t getOffsetDeg() {
    Preferences p;
    p.begin(NVS_NS, /*readOnly=*/true);
    uint16_t v = p.getUShort(NVS_KEY_OFFSET, 0);
    p.end();
    return (v == 180) ? 180 : 0;
}

uint8_t computeLgfxRotation() {
    const uint16_t off = getOffsetDeg();
    const uint8_t step = (off == 180) ? 2 : 0;
    return static_cast<uint8_t>((HW_DISPLAY_ROTATION + step) % 4);
}

void applyAndReboot(uint16_t offsetDeg) {
    const uint16_t normalized = (offsetDeg == 180) ? 180 : 0;

    Preferences p;
    p.begin(NVS_NS, /*readOnly=*/false);
    p.putUShort(NVS_KEY_OFFSET, normalized);
    p.end();

    // Touch calibration data is rotation-dependent — clearing it forces the
    // first-boot calibration crosshairs to run again so coordinates match the
    // new orientation.
    Preferences cal;
    cal.begin(TOUCH_NVS_NS, /*readOnly=*/false);
    cal.remove(TOUCH_NVS_KEY_CAL);
    cal.end();

    LOG_INFO("Rotation", "Saved offset=%u° — invalidated touch cal — rebooting", normalized);
    delay(150);
    esp_restart();
}

} // namespace RotationConfig
