#pragma once

#include <stddef.h>
#include <stdint.h>

namespace AlertBroadcast {

void publish(const uint8_t *frame, size_t len);

[[nodiscard]] size_t copyLatest(uint8_t *out, size_t outSize, uint32_t *generation);

}
