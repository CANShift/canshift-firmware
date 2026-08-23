#pragma once

#include "ui/control_vocabulary.h"

#include <stdint.h>

namespace ControlSplash {

void raiseFor(const ControlVocabulary::Control &control, ControlVocabulary::ControlState state,
              uint8_t level);

void update();

} // namespace ControlSplash
