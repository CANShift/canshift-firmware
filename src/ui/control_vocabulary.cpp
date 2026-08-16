#include "control_vocabulary.h"

#include <stdio.h>
#include <string.h>

namespace ControlVocabulary {

namespace {

constexpr size_t kParamDigits = 12;

constexpr Control kControls[] = {
    {ControlId::ANTI_LAG,
     "ANTI-LAG",
     ControlKind::TOGGLE,
     ArmedState::NONE,
     0,
     {{"", "OFF", ControlParam::NONE},
      {"", "", ControlParam::NONE},
      {"", "ON", ControlParam::NONE},
      {"EGT HIGH", "LOCKED", ControlParam::NONE}}},
    {ControlId::TRACTION,
     "TRACTION",
     ControlKind::STEPPER,
     ArmedState::PHYSICAL,
     0,
     {{"", "OFF", ControlParam::NONE},
      {"", "LEVEL %d", ControlParam::LEVEL},
      {"CUTTING", "LEVEL %d", ControlParam::LEVEL},
      {"NO WHEEL SPEED", "N/A", ControlParam::NONE}}},
    {ControlId::LAUNCH,
     "LAUNCH",
     ControlKind::TOGGLE,
     ArmedState::PHYSICAL,
     0,
     {{"", "OFF", ControlParam::NONE},
      {"%d rpm", "ARMED", ControlParam::RPM},
      {"HOLDING", "%d", ControlParam::RPM},
      {"MOVING", "LOCKED", ControlParam::NONE}}},
    {ControlId::PIT_LIMIT,
     "PIT LIMIT",
     ControlKind::TOGGLE,
     ArmedState::NONE,
     0,
     {{"", "OFF", ControlParam::NONE},
      {"", "", ControlParam::NONE},
      {"HOLDING", "%d", ControlParam::SPEED_KPH},
      {"GEAR %d", "LOCKED", ControlParam::GEAR}}},
    {ControlId::CRUISE,
     "CRUISE",
     ControlKind::TOGGLE,
     ArmedState::PHYSICAL,
     0,
     {{"", "OFF", ControlParam::NONE},
      {"SET %d", "ARMED", ControlParam::SPEED_KPH},
      {"HOLDING", "%d", ControlParam::SPEED_KPH},
      {"BRAKE CUT", "CANCELLED", ControlParam::NONE}}},
    {ControlId::ECU_MAP,
     "ECU MAP",
     ControlKind::STEPPER,
     ArmedState::NONE,
     1,
     {{"", "MAP %d", ControlParam::LEVEL},
      {"", "", ControlParam::NONE},
      {"", "MAP %d", ControlParam::LEVEL},
      {"NO ECU", "N/A", ControlParam::NONE}}},
};

constexpr size_t kControlCount = sizeof(kControls) / sizeof(kControls[0]);

static_assert(kControlCount == static_cast<size_t>(ControlId::COUNT),
              "every ControlId needs one vocabulary row");

constexpr bool armedRowMatchesTheFlag(const Control &control) {
    return (control.phrases[static_cast<uint8_t>(ControlState::ARMED)].stateWord[0] == '\0') ==
           (control.armed == ArmedState::NONE);
}

constexpr bool armedRowsMatchTheFlag(size_t index = 0) {
    return index >= kControlCount ||
           (armedRowMatchesTheFlag(kControls[index]) && armedRowsMatchTheFlag(index + 1));
}

static_assert(armedRowsMatchTheFlag(),
              "a control with ArmedState::NONE must carry an empty armed phrase, and vice versa");

size_t appendLiteral(char *out, size_t outLen, size_t written, const char *text) {
    for (size_t i = 0; text[i] != '\0' && written + 1 < outLen; ++i)
        out[written++] = text[i];
    return written;
}

size_t appendExpanded(char *out, size_t outLen, size_t written, const char *tmpl, int param) {
    char digits[kParamDigits];
    snprintf(digits, sizeof(digits), "%d", param);
    for (size_t i = 0; tmpl[i] != '\0' && written + 1 < outLen; ++i) {
        if (tmpl[i] == '%' && tmpl[i + 1] == 'd') {
            written = appendLiteral(out, outLen, written, digits);
            ++i;
            continue;
        }
        out[written++] = tmpl[i];
    }
    return written;
}

} // namespace

const Control *find(const char *kicker) {
    if (!kicker || kicker[0] == '\0')
        return nullptr;
    for (const Control &control : kControls) {
        if (strcmp(control.kicker, kicker) == 0)
            return &control;
    }
    return nullptr;
}

bool hasArmedState(const Control &control) {
    return control.armed == ArmedState::PHYSICAL;
}

const Phrase &phraseFor(const Control &control, ControlState state) {
    const uint8_t idx = static_cast<uint8_t>(state);
    return control.phrases[idx < kStateCount ? idx : 0];
}

void composeKicker(const Control &control, ControlState state, int param, char *out,
                   size_t outLen) {
    joinPhrase(control.kicker, kStack, phraseFor(control, state).kickerSuffix, param, out, outLen);
}

void composeStateWord(const Control &control, ControlState state, int param, char *out,
                      size_t outLen) {
    if (!out || outLen == 0)
        return;
    out[appendExpanded(out, outLen, 0, phraseFor(control, state).stateWord, param)] = '\0';
}

ControlState stateFromRaw(uint8_t raw) {
    return raw < kStateCount ? static_cast<ControlState>(raw) : ControlState::OFF;
}

void joinPhrase(const char *lead, const char *joiner, const char *tail, int param, char *out,
                size_t outLen) {
    if (!out || outLen == 0)
        return;
    size_t written = appendExpanded(out, outLen, 0, lead ? lead : "", param);
    if (tail && tail[0] != '\0') {
        if (written > 0)
            written = appendLiteral(out, outLen, written, joiner ? joiner : kSeparator);
        written = appendExpanded(out, outLen, written, tail, param);
    }
    out[written] = '\0';
}

} // namespace ControlVocabulary
