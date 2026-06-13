#include "rotation_config.h"
#include "app_config.h"
#include "hardware_profile.h"
#include "diag/logger.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>

static constexpr char NVS_NS[] = "display_cfg";
static constexpr char NVS_KEY_OFFSET[] = "rot_offset";

static constexpr char TOUCH_NVS_NS[] = "touch";
static constexpr char TOUCH_NVS_KEY_CAL[] = "cal";

namespace RotationConfig {

uint16_t getOffsetDeg() {
    Preferences p;
    p.begin(NVS_NS, true);
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
    p.begin(NVS_NS, false);
    p.putUShort(NVS_KEY_OFFSET, normalized);
    p.end();

    Preferences cal;
    cal.begin(TOUCH_NVS_NS, false);
    cal.remove(TOUCH_NVS_KEY_CAL);
    cal.end();

    LOG_INFO("Rotation", "Saved offset=%u° — invalidated touch cal — rebooting", normalized);
    delay(PRE_RESTART_FLUSH_DELAY_MS);
    esp_restart();
}

} // namespace RotationConfig
