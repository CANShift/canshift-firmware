
#pragma once

#include <cstdint>

namespace TimerService {

enum class State : uint8_t {
    Reset = 0,
    Running = 1,
    Paused = 2,
};

struct Snapshot {
    State state;
    uint32_t elapsedMs;
    uint32_t version;
};

void init();

[[nodiscard]] bool start();
[[nodiscard]] bool pause();
[[nodiscard]] bool resume();
[[nodiscard]] bool reset();

Snapshot snapshot();
State getState();
uint32_t getElapsedMs();

} // namespace TimerService
