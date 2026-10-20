#pragma once

#include "imu_core_rs.h"

#include <stdint.h>

namespace ImuFeed {

constexpr uint32_t kStaleAfterMs = 250;
constexpr uint8_t kLevelNotSaved = kImuLevelScreenFlat + 1;

[[nodiscard]] bool reserve();
void markPresent();
[[nodiscard]] bool present();

void publish(const ImuCoreState &core);
[[nodiscard]] bool latest(ImuCoreState &out);

void requestPeakReset();
[[nodiscard]] bool takePeakReset();

void requestLevel();
[[nodiscard]] bool takeLevelRequest();
void reportLevel(uint8_t status);
[[nodiscard]] uint8_t levelStatus();

}
