#pragma once

#include <stdint.h>

#include "can/signal_map.h"

namespace Obd2Poller {

void init();

void tick(uint32_t nowMs);

bool onRxFrame(uint32_t frameId, const uint8_t *data, uint8_t length);

uint32_t pollsSent();

uint32_t responsesMatched();

uint32_t responsesMissed();

uint8_t activeSlotCount();

}
