#pragma once

#include "config/config_types.h"

#include <stddef.h>
#include <stdint.h>

namespace TimerSources {

struct Inputs {
    uint32_t elapsedMs;
    uint16_t stopwatchLaps;
    uint32_t currentLapMs;
    uint32_t lastLapMs;
    uint32_t bestLapMs;
    uint16_t trackLapNumber;
    int32_t deltaMs;
    bool trackActive;
};

inline constexpr size_t kTextCapacity = 12;

void render(CfgTimerSource source, const Inputs &in, char *buf, size_t cap);

bool isInteractive(CfgTimerSource source);

const char *kicker(CfgTimerSource source);

} // namespace TimerSources
