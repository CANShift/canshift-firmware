#pragma once

#include "config/config_types.h"
#include "runtime/timer_service.h"
#include "runtime/track_store.h"

#include <stddef.h>
#include <stdint.h>

namespace TimerSources {

struct Inputs {
    uint32_t elapsedMs;
    uint32_t currentLapMs;
    uint32_t lastLapMs;
    uint32_t bestLapMs;
    uint16_t lapCount;
    int32_t deltaMs;
    bool trackActive;
};

inline constexpr size_t kTextCapacity = 12;

Inputs select(const TimerService::Snapshot &stopwatch, const TrackStore::State &track,
              bool trackActive);

[[nodiscard]] bool render(CfgTimerSource source, const Inputs &in, char *buf, size_t cap);

void formatStopwatch(char *buf, size_t len, uint32_t elapsedMs, bool msec, bool blinkOn);

bool isInteractive(CfgTimerSource source);

const char *kicker(CfgTimerSource source);

const char *unit(CfgTimerSource source);

}
