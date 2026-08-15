#pragma once

#include "ui/control_vocabulary.h"

#include <stdint.h>

namespace ControlSplashCopy {

enum class Tone : uint8_t { ENGAGE, DISENGAGE, REFUSE };

constexpr uint8_t kToneCount = 3;

enum class Hero : uint8_t { WORD, PARAM, LEVEL };

enum class Bottom : uint8_t { NONE, LINE, CELLS, SEGMENTS };

enum class Sub : uint8_t { NONE, STATE_WORD, LEVEL_NOTE, REASON };

struct Copy {
    const char *name;
    Hero hero;
    Bottom engagedBottom;
    const char *note;
    const char *engagedLead;
    const char *engagedTail;
    const char *offLead;
    const char *offTail;
    const char *reason;
    const char *reasonUnit;
    bool reasonStatesTheReading;
    const char *remedy;
};

const Copy &forControl(ControlVocabulary::ControlId id);

Tone toneFor(const ControlVocabulary::Control &control, ControlVocabulary::ControlState state,
             uint8_t level);

const char *unitFor(ControlVocabulary::ControlParam param);

} // namespace ControlSplashCopy
