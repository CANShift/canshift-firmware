#pragma once

#include <stdint.h>

namespace AlertTakeover {

void init();

void update();

void reset();

bool isActive();

void requestAcknowledge();

void requestAcknowledgeSource(uint8_t source);

}
