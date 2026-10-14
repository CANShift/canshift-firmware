#pragma once

#include "app_config.h"

namespace WorkshopMode {

#if APP_WORKSHOP_ENABLED

[[nodiscard]] bool boardSupported();

[[nodiscard]] bool offered();

[[nodiscard]] bool request();

[[nodiscard]] bool isEntering();

[[nodiscard]] bool isActive();

void settleEntry(bool entered);

#else

[[nodiscard]] inline bool boardSupported() {
    return false;
}

[[nodiscard]] inline bool offered() {
    return false;
}

[[nodiscard]] inline bool request() {
    return false;
}

[[nodiscard]] inline bool isEntering() {
    return false;
}

[[nodiscard]] inline bool isActive() {
    return false;
}

inline void settleEntry(bool) {}

#endif

} // namespace WorkshopMode
