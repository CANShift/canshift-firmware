#pragma once

#include <stdint.h>

#include "can/signal_map.h"

namespace Obd2Poller {

void init();

void tick(uint32_t nowMs);

bool onRxFrame(uint32_t frameId, const uint8_t *data, uint8_t length);

}
