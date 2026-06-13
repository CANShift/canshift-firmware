#pragma once

#include <stdint.h>

#include <esp_err.h>

namespace CanManager {

void reserveInitTaskStack();

[[nodiscard]] esp_err_t initHardware();

[[nodiscard]] bool isAvailable();

[[nodiscard]] bool tick();

bool sendFrame(uint32_t id, const uint8_t *data, uint8_t len, bool extended = false);

} // namespace CanManager
