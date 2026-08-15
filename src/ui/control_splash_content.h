#pragma once

#include "ui/control_splash_copy.h"
#include "ui/control_vocabulary.h"

#include <stddef.h>
#include <stdint.h>

namespace ControlSplashContent {

constexpr size_t kNameLen = 20;
constexpr size_t kHeroLen = 12;
constexpr size_t kUnitLen = 8;
constexpr size_t kLineLen = 40;
constexpr size_t kCellKickerLen = 14;
constexpr size_t kCellValueLen = 10;
constexpr uint8_t kMaxCells = 2;

struct Cell {
    char kicker[kCellKickerLen];
    char value[kCellValueLen];
    char unit[kUnitLen];
};

struct Content {
    ControlSplashCopy::Tone tone;
    ControlSplashCopy::Bottom bottom;
    ControlSplashCopy::Sub sub;
    char name[kNameLen];
    char hero[kHeroLen];
    char heroUnit[kUnitLen];
    char subText[kLineLen];
    char line[kLineLen];
    Cell cells[kMaxCells];
    uint8_t cellCount;
    uint8_t segmentsLit;
};

void compose(const ControlVocabulary::Control &control, ControlVocabulary::ControlState state,
             uint8_t level, Content &out);

} // namespace ControlSplashContent
