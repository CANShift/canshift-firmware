#pragma once

#include <esp_err.h>
#include <stdint.h>

namespace CanDriverInstall {

enum class BusMode : uint8_t { Normal, ListenOnly };

void reserveTaskStack();

[[nodiscard]] esp_err_t installOnCore0(bool *installed);

[[nodiscard]] esp_err_t startOnThisCore(BusMode mode, uint16_t speedKbps);

[[nodiscard]] esp_err_t stopOnThisCore();

[[nodiscard]] uint16_t configuredSpeedKbps();

}
