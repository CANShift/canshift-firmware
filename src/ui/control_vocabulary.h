#pragma once

#include "control_state_rs.h"

#include <stddef.h>
#include <stdint.h>

namespace ControlVocabulary {

inline constexpr const char *kSeparator = " · ";
inline constexpr const char *kDash = " — ";
inline constexpr const char *kStack = "\n";

enum class ControlState : uint8_t {
    OFF = kControlStateOff,
    ARMED = kControlStateArmed,
    ACTIVE = kControlStateActive,
    UNAVAILABLE = kControlStateUnavailable,
};

constexpr uint8_t kStateCount = kControlStateCount;

enum class ControlId : uint8_t {
    ANTI_LAG = 0,
    TRACTION,
    LAUNCH,
    PIT_LIMIT,
    CRUISE,
    ECU_MAP,
    COUNT,
};

enum class ControlKind : uint8_t { TOGGLE, STEPPER };

enum class ArmedState : uint8_t { NONE, PHYSICAL };

enum class ControlParam : uint8_t { NONE, LEVEL, RPM, SPEED_KPH, GEAR };

struct Phrase {
    const char *kickerSuffix;
    const char *stateWord;
    ControlParam param;
};

struct Control {
    ControlId id;
    const char *kicker;
    ControlKind kind;
    ArmedState armed;
    uint8_t stepFloor;
    Phrase phrases[kStateCount];
};

const Control *find(const char *kicker);

bool hasArmedState(const Control &control);

const Phrase &phraseFor(const Control &control, ControlState state);

void composeKicker(const Control &control, ControlState state, int param, char *out, size_t outLen);

void composeStateWord(const Control &control, ControlState state, int param, char *out,
                      size_t outLen);

ControlState stateFromRaw(uint8_t raw);

void joinPhrase(const char *lead, const char *joiner, const char *tail, int param, char *out,
                size_t outLen);

}
