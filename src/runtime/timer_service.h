
#pragma once

#include <cstdint>

namespace TimerService {

enum class State : uint8_t {
    Reset = 0,
    Running = 1,
    Paused = 2,
};

struct Lap {
    uint16_t index;
    uint16_t sessionId;
    uint32_t lapMs;
    uint32_t totalMs;
};

struct Snapshot {
    State state;
    uint32_t elapsedMs;
    uint32_t version;
    uint16_t lapCount;
    uint16_t sessionId;
};

void init();

[[nodiscard]] bool start();
[[nodiscard]] bool pause();
[[nodiscard]] bool resume();
[[nodiscard]] bool reset();
[[nodiscard]] bool lap();

Snapshot snapshot();
State getState();
uint32_t getElapsedMs();

uint8_t pendingLapCount();
[[nodiscard]] bool popPendingLap(Lap &out);

} // namespace TimerService
