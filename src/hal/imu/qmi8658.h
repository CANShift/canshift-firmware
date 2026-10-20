#pragma once

#include "board_profile.h"

#include <stdint.h>

namespace Qmi8658 {

[[nodiscard]] bool begin(const canshift::boards::ImuProfile &imu);
[[nodiscard]] bool readAccelG(const canshift::boards::ImuProfile &imu, float (&out)[3]);

}
