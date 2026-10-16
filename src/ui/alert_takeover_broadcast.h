#pragma once

#include <stdint.h>

namespace AlertTakeoverBroadcast {

void publish(uint32_t critical, uint32_t acknowledged, int displayed, uint32_t nowMs);

} // namespace AlertTakeoverBroadcast
