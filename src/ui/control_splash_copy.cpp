#include "control_splash_copy.h"

#include <stddef.h>

namespace ControlSplashCopy {

namespace {

using ControlVocabulary::ControlId;
using ControlVocabulary::ControlKind;
using ControlVocabulary::ControlParam;
using ControlVocabulary::ControlState;
using ControlVocabulary::kStateCount;

constexpr Copy kCopies[] = {
    {"ANTI-LAG", Hero::WORD, Bottom::CELLS, "", "", "", "EGT FALLING", "%d °C", "EGT IS HIGH", "°C",
     true, "WAIT FOR EGT TO DROP"},
    {"TRACTION CONTROL", Hero::LEVEL, Bottom::SEGMENTS, "MORE SLIP", "", "", "", "",
     "NO WHEEL SPEED", "", false, "CHECK THE WHEEL SENSORS"},
    {"LAUNCH CONTROL", Hero::PARAM, Bottom::LINE, "", "CLUTCH IN", "RELEASE TO GO", "", "",
     "CAR IS MOVING", "km/h", true, "STOP TO ARM"},
    {"PIT LIMIT", Hero::PARAM, Bottom::NONE, "", "", "", "", "", "GEAR IS TOO HIGH", "", true,
     "SHIFT DOWN TO ARM"},
    {"CRUISE", Hero::PARAM, Bottom::NONE, "", "", "", "", "", "BRAKE PRESSED", "", false,
     "RELEASE THE BRAKE"},
    {"ECU MAP", Hero::LEVEL, Bottom::CELLS, "", "", "", "", "", "NO ECU LINK", "", false,
     "CHECK THE ECU CONNECTION"},
};

constexpr size_t kCopyCount = sizeof(kCopies) / sizeof(kCopies[0]);

static_assert(kCopyCount == static_cast<size_t>(ControlId::COUNT),
              "every ControlId needs one splash copy row");

constexpr Tone kToneByState[kStateCount] = {Tone::DISENGAGE, Tone::ENGAGE, Tone::ENGAGE,
                                            Tone::REFUSE};

struct ParamUnit {
    ControlParam param;
    const char *unit;
};

constexpr ParamUnit kParamUnits[] = {{ControlParam::RPM, " rpm"},
                                     {ControlParam::SPEED_KPH, " km/h"}};

} // namespace

const Copy &forControl(ControlId id) {
    const size_t idx = static_cast<size_t>(id);
    return kCopies[idx < kCopyCount ? idx : 0];
}

Tone toneFor(const ControlVocabulary::Control &control, ControlState state, uint8_t level) {
    const uint8_t idx = static_cast<uint8_t>(state);
    const Tone base = kToneByState[idx < kStateCount ? idx : 0];
    if (base == Tone::REFUSE || control.kind == ControlKind::TOGGLE)
        return base;
    return level > 0 ? Tone::ENGAGE : Tone::DISENGAGE;
}

const char *unitFor(ControlParam param) {
    for (const ParamUnit &row : kParamUnits) {
        if (row.param == param)
            return row.unit;
    }
    return "";
}

} // namespace ControlSplashCopy
