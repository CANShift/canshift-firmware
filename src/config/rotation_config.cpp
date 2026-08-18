#include "rotation_config.h"
#include "app_config.h"
#include "board.h"
#include "config/board_profile_loader.h"
#include "diag/error_store.h"
#include "diag/logger.h"
#include "hal/storage/nvs_store.h"

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
    const uint8_t boardRotation = canshift::boards::runtimeBoardProfile().lcd.default_rotation;
    return static_cast<uint8_t>((boardRotation + step) % 4);
}

void applyAndReboot(uint16_t offsetDeg) {
    const uint16_t normalized = (offsetDeg == 180) ? 180 : 0;

    if (!NvsStore::putUShort(NVS_NS, NVS_KEY_OFFSET, normalized)) {
        LOG_ERROR("Rotation", "offset write failed — touch cal kept, rebooting unchanged");
        ErrorStore::push(ERROR_SRC_SYSTEM, "nvs_write", "rotation offset not persisted");
        delay(PRE_RESTART_FLUSH_DELAY_MS);
        esp_restart();
    }

    if (!NvsStore::remove(TOUCH_NVS_NS, TOUCH_NVS_KEY_CAL)) {
        LOG_WARN("Rotation", "touch cal invalidation failed — recalibrate manually if skewed");
    }

    LOG_INFO("Rotation", "Saved offset=%u° — invalidated touch cal — rebooting", normalized);
    delay(PRE_RESTART_FLUSH_DELAY_MS);
    esp_restart();
}

} // namespace RotationConfig
