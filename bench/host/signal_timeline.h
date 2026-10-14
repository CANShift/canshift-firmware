#pragma once

#include <cstdint>
#include <cstdio>

namespace SignalTimeline {

void reset();

void writeFrame(FILE *out, uint32_t atMs, const char *direction, uint32_t id, const uint8_t *data,
                uint8_t length);

void writeChanges(FILE *out, uint32_t atMs);

} // namespace SignalTimeline
