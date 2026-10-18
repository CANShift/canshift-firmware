#pragma once

#include <stdbool.h>
#include <stdint.h>

namespace TrackStore {

struct State {
    bool trackMode;
    uint32_t currentLapMs;
    uint32_t lastLapMs;
    uint32_t bestLapMs;
    uint16_t lapNumber;
    int32_t deltaMs;
    bool isBestLap;
    uint32_t lastUpdateMs;
};

void init();

void setTelemetry(const State &next);

void snapshot(State *out);

bool isActiveWithin(uint32_t timeoutMs);

}
