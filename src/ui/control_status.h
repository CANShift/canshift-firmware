#pragma once

#include "can/signal_map.h"
#include "ui/control_vocabulary.h"

#include <stdint.h>

namespace ControlStatus {

struct Request {
    bool engaged;
    uint8_t level;
};

bool isConfirming(SignalId signal);

ControlVocabulary::ControlState evaluate(const ControlVocabulary::Control &control, SignalId signal,
                                         const Request &request);

int paramValue(ControlVocabulary::ControlParam param, uint8_t level);

bool levelFromSignal(ControlVocabulary::ControlId id, uint8_t *outLevel);

struct GuardReading {
    float value;
    float limit;
    bool valid;
};

GuardReading guardReading(ControlVocabulary::ControlId id);

} // namespace ControlStatus
