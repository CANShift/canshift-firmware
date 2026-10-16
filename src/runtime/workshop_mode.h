#pragma once

#include "app_config.h"

#include <stdint.h>

namespace WorkshopMode {

#if APP_WORKSHOP_ENABLED

[[nodiscard]] bool boardSupported();

[[nodiscard]] bool offered();

[[nodiscard]] bool request();

[[nodiscard]] bool isEntering();

[[nodiscard]] bool isActive();

void settleEntry(bool entered);

void armResumeAfterUpdate();

[[nodiscard]] bool takeResumeRequest();

void resumeAfterUpdate(bool requested, bool imagePendingVerify);

void leaveOnMotion(uint32_t nowMs);

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

inline void armResumeAfterUpdate() {}

[[nodiscard]] inline bool takeResumeRequest() {
    return false;
}

inline void resumeAfterUpdate(bool, bool) {}

inline void leaveOnMotion(uint32_t) {}

#endif

} // namespace WorkshopMode
