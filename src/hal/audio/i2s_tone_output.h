#pragma once

#include "board_profile.h"

#include <stddef.h>
#include <stdint.h>

namespace I2sToneOutput {

[[nodiscard]] bool begin(const canshift::boards::AudioProfile &audio);

[[nodiscard]] bool write(const int16_t *samples, size_t count);

}
