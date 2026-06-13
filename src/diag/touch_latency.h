#pragma once

#include <stdint.h>

namespace TouchLatency {

void recordPressNow();

void consumePressAndWarnIfSlow();

} // namespace TouchLatency
