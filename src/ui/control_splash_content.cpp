#include "control_splash_content.h"

#include "config/config_loader.h"
#include "control_state_rs.h"
#include "ui/control_status.h"
#include "util/format_float.h"

#include <stdio.h>
#include <string.h>

namespace ControlSplashContent {

namespace {

using ControlSplashCopy::Bottom;
using ControlSplashCopy::Copy;
using ControlSplashCopy::Hero;
using ControlSplashCopy::Sub;
using ControlSplashCopy::Tone;
using ControlVocabulary::Control;
using ControlVocabulary::ControlId;
using ControlVocabulary::ControlState;

constexpr const char *kEgtUnit = " °C";
constexpr const char *kRpmUnit = " rpm";

struct Ingredients {
    const Control &control;
    ControlState state;
    uint8_t level;
    int param;
    const Copy &copy;
    ControlStatus::GuardReading guard;
};

void copyText(char *out, size_t outLen, const char *text) {
    snprintf(out, outLen, "%s", text ? text : "");
}

void formatReading(char *out, size_t outLen, float value, const char *unit) {
    char digits[12];
    FloatFormat::formatFixed(digits, sizeof(digits), value, 0);
    if (unit[0] == '\0') {
        copyText(out, outLen, digits);
        return;
    }
    snprintf(out, outLen, "%s %s", digits, unit);
}

Hero heroKind(const Ingredients &in, Tone tone) {
    return tone == Tone::ENGAGE ? in.copy.hero : Hero::WORD;
}

void composeHero(const Ingredients &in, Content &out) {
    const Hero hero = heroKind(in, out.tone);
    if (hero == Hero::WORD) {
        ControlVocabulary::composeStateWord(in.control, in.state, in.param, out.hero,
                                            sizeof(out.hero));
        return;
    }
    if (hero == Hero::LEVEL) {
        snprintf(out.hero, sizeof(out.hero), "%u", static_cast<unsigned>(in.level));
        return;
    }
    snprintf(out.hero, sizeof(out.hero), "%d", in.param);
    copyText(out.heroUnit, sizeof(out.heroUnit),
             ControlSplashCopy::unitFor(ControlVocabulary::phraseFor(in.control, in.state).param));
}

void composeReason(const Ingredients &in, Content &out) {
    out.sub = Sub::REASON;
    char tail[16] = "";
    if (in.copy.reasonStatesTheReading && in.guard.valid)
        formatReading(tail, sizeof(tail), in.guard.value, in.copy.reasonUnit);
    ControlVocabulary::joinPhrase(in.copy.reason, ControlVocabulary::kDash, tail, 0, out.subText,
                                  sizeof(out.subText));
}

void composeStateWordSub(const Ingredients &in, Content &out) {
    char word[kLineLen];
    ControlVocabulary::composeStateWord(in.control, in.state, in.param, word, sizeof(word));
    if (strcmp(word, out.hero) == 0)
        return;
    out.sub = Sub::STATE_WORD;
    copyText(out.subText, sizeof(out.subText), word);
}

void composeLevelNote(const Ingredients &in, Content &out) {
    out.sub = Sub::LEVEL_NOTE;
    char lead[12];
    snprintf(lead, sizeof(lead), "OF %u", static_cast<unsigned>(CONTROL_STEP_MAX));
    ControlVocabulary::joinPhrase(lead, ControlVocabulary::kSeparator, in.copy.note, 0, out.subText,
                                  sizeof(out.subText));
}

void composeSub(const Ingredients &in, Content &out) {
    if (out.tone == Tone::REFUSE) {
        composeReason(in, out);
        return;
    }
    const Hero hero = heroKind(in, out.tone);
    if (hero == Hero::PARAM) {
        composeStateWordSub(in, out);
        return;
    }
    if (hero == Hero::LEVEL && in.copy.note[0] != '\0')
        composeLevelNote(in, out);
}

void setCell(Cell &cell, const char *kicker, float value, const char *unit) {
    copyText(cell.kicker, sizeof(cell.kicker), kicker);
    FloatFormat::formatFixed(cell.value, sizeof(cell.value), value, 0);
    copyText(cell.unit, sizeof(cell.unit), unit);
}

void fillAntiLagCells(const Ingredients &in, Content &out) {
    if (!in.guard.valid)
        return;
    setCell(out.cells[0], "EGT", in.guard.value, kEgtUnit);
    setCell(out.cells[1], "CUT LIMIT", in.guard.limit, kEgtUnit);
    out.cellCount = 2;
}

void fillMapCells(const Ingredients &, Content &out) {
    const float limitRpm = ConfigLoader::getDashboardConfig().revLimitRpm;
    if (limitRpm <= 0.0f)
        return;
    setCell(out.cells[0], "LIMIT", limitRpm, kRpmUnit);
    out.cellCount = 1;
}

using Fill = void (*)(const Ingredients &, Content &);

struct CellSource {
    ControlId id;
    Fill fill;
};

constexpr CellSource kCellSources[] = {{ControlId::ANTI_LAG, fillAntiLagCells},
                                       {ControlId::ECU_MAP, fillMapCells}};

void fillCells(const Ingredients &in, Content &out) {
    for (const CellSource &row : kCellSources) {
        if (row.id == in.control.id)
            row.fill(in, out);
    }
}

void fillNothing(const Ingredients &, Content &) {}

void fillEngagedLine(const Ingredients &in, Content &out) {
    ControlVocabulary::joinPhrase(in.copy.engagedLead, ControlVocabulary::kSeparator,
                                  in.copy.engagedTail, in.param, out.line, sizeof(out.line));
}

void fillSegments(const Ingredients &in, Content &out) {
    out.segmentsLit = in.level;
}

constexpr Fill kEngagedFills[] = {fillNothing, fillEngagedLine, fillCells, fillSegments};

void bottomEngaged(const Ingredients &in, Content &out) {
    out.bottom = in.copy.engagedBottom;
    kEngagedFills[static_cast<uint8_t>(out.bottom)](in, out);
}

void bottomDisengaged(const Ingredients &in, Content &out) {
    if (in.copy.offLead[0] == '\0')
        return;
    out.bottom = Bottom::LINE;
    const char *tail = in.guard.valid ? in.copy.offTail : "";
    ControlVocabulary::joinPhrase(in.copy.offLead, ControlVocabulary::kSeparator, tail,
                                  static_cast<int>(in.guard.value), out.line, sizeof(out.line));
}

void bottomRefused(const Ingredients &in, Content &out) {
    out.bottom = Bottom::LINE;
    copyText(out.line, sizeof(out.line), in.copy.remedy);
}

constexpr Fill kBottomFills[ControlSplashCopy::kToneCount] = {bottomEngaged, bottomDisengaged,
                                                              bottomRefused};

} // namespace

void compose(const Control &control, ControlState state, uint8_t level, Content &out) {
    out = Content{};
    const Ingredients in = {
        control,
        state,
        level,
        ControlStatus::paramValue(ControlVocabulary::phraseFor(control, state).param, level),
        ControlSplashCopy::forControl(control.id),
        ControlStatus::guardReading(control.id)};
    out.tone = ControlSplashCopy::toneFor(control, state, level);
    copyText(out.name, sizeof(out.name), in.copy.name);
    composeHero(in, out);
    composeSub(in, out);
    kBottomFills[static_cast<uint8_t>(out.tone)](in, out);
}

} // namespace ControlSplashContent
