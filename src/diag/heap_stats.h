#pragma once

#include <stdint.h>

namespace HeapStats {

struct Snapshot {
    uint32_t tsMs;
    uint32_t freeInternal;
    uint32_t largestInternal;
    uint32_t freePsram;
    uint32_t largestPsram;
    bool hasPsram;
};

Snapshot sampleNow();

const Snapshot &latest();

void tick();

void emitNow();

void logHeapBracket(const char *tag);

} // namespace HeapStats
