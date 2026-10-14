#pragma once

#include <stdint.h>

namespace CanRxRoute {

void deliver(uint32_t frameId, const uint8_t *data, uint8_t length);

} // namespace CanRxRoute
