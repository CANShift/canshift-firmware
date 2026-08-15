#include "control_vocabulary.h"

#include <stdio.h>
#include <string.h>

namespace ControlVocabulary {

namespace {

constexpr const char *kSeparator = " · ";
constexpr size_t kParamDigits = 12;

constexpr Control kControls[] = {
    {ControlId::ANTI_LAG,
     "ANTI-LAG",
     ControlKind::TOGGLE,
     0,
     {{"", "OFF", ControlParam::NONE},
      {"", "ARMED", ControlParam::NONE},
      {"", "ON", ControlParam::NONE},
      {"EGT HIGH", "LOCKED", ControlParam::NONE}}},
    {ControlId::TRACTION,
     "TRACTION",
     ControlKind::STEPPER,
     0,
     {{"", "OFF", ControlParam::NONE},
      {"", "LEVEL %d", ControlParam::LEVEL},
      {"CUTTING", "LEVEL %d", ControlParam::LEVEL},
      {"NO WHEEL SPEED", "N/A", ControlParam::NONE}}},
    {ControlId::LAUNCH,
     "LAUNCH",
     ControlKind::TOGGLE,
     0,
     {{"", "OFF", ControlParam::NONE},
      {"%d rpm", "ARMED", ControlParam::RPM},
      {"HOLDING", "%d", ControlParam::RPM},
      {"MOVING", "LOCKED", ControlParam::NONE}}},
    {ControlId::PIT_LIMIT,
     "PIT LIMIT",
     ControlKind::TOGGLE,
     0,
     {{"", "OFF", ControlParam::NONE},
      {"%d km/h", "READY", ControlParam::SPEED_KPH},
      {"HOLDING", "%d", ControlParam::SPEED_KPH},
      {"GEAR %d", "LOCKED", ControlParam::GEAR}}},
    {ControlId::CRUISE,
     "CRUISE",
     ControlKind::TOGGLE,
     0,
     {{"", "OFF", ControlParam::NONE},
      {"SET %d", "ARMED", ControlParam::SPEED_KPH},
      {"HOLDING", "%d", ControlParam::SPEED_KPH},
      {"BRAKE CUT", "CANCELLED", ControlParam::NONE}}},
    {ControlId::ECU_MAP,
     "ECU MAP",
     ControlKind::STEPPER,
     1,
     {{"", "MAP %d", ControlParam::LEVEL},
      {"", "MAP %d", ControlParam::LEVEL},
      {"", "MAP %d", ControlParam::LEVEL},
      {"NO ECU", "N/A", ControlParam::NONE}}},
};

constexpr size_t kControlCount = sizeof(kControls) / sizeof(kControls[0]);

static_assert(kControlCount == static_cast<size_t>(ControlId::COUNT),
              "every ControlId needs one vocabulary row");

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

const Phrase &phraseFor(const Control &control, ControlState state) {
    const uint8_t idx = static_cast<uint8_t>(state);
    return control.phrases[idx < kStateCount ? idx : 0];
}

void composeKicker(const Control &control, ControlState state, int param, char *out,
                   size_t outLen) {
    if (!out || outLen == 0)
        return;
    const Phrase &phrase = phraseFor(control, state);
    size_t written = appendLiteral(out, outLen, 0, control.kicker);
    if (phrase.kickerSuffix[0] != '\0') {
        written = appendLiteral(out, outLen, written, kSeparator);
        written = appendExpanded(out, outLen, written, phrase.kickerSuffix, param);
    }
    out[written] = '\0';
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

} // namespace ControlVocabulary
