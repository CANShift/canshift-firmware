#pragma once

#include "top_bar_internal.h"

#include <stdint.h>

namespace TopBarStatusUpdate {

void updateTimer(uint8_t idx, TopBarInternal::DynItem &d, bool silent);

void updateFlagWord(uint8_t idx, TopBarInternal::DynItem &d, bool silent);

}
