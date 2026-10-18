#pragma once

#include "config/dashboard_outcome.h"

#include <atomic>
#include <cstddef>
#include <stdint.h>

namespace ConfigReloadStatus {

enum class State : uint8_t {
    Idle = 0,
    Pending,
    Applied,
    Refused,
    Unchecked,
};

inline constexpr const char *kWireNames[] = {"idle", "pending", "applied", "refused", "unchecked"};

static_assert(sizeof(kWireNames) / sizeof(kWireNames[0]) ==
                  static_cast<size_t>(State::Unchecked) + 1,
              "every reload state needs a wire name");

inline constexpr State kStateForOutcome[ConfigLoader::kDashboardOutcomeCount] = {
    State::Applied, State::Refused, State::Refused, State::Refused, State::Unchecked,
};

inline std::atomic<State> s_state{State::Idle};

inline void markPending() {
    s_state.store(State::Pending, std::memory_order_release);
}

inline void settle(ConfigLoader::DashboardOutcome outcome) {
    s_state.store(kStateForOutcome[static_cast<uint8_t>(outcome)], std::memory_order_release);
}

[[nodiscard]] inline bool refused() {
    return s_state.load(std::memory_order_acquire) == State::Refused;
}

[[nodiscard]] inline const char *wireName() {
    return kWireNames[static_cast<uint8_t>(s_state.load(std::memory_order_acquire))];
}

}
