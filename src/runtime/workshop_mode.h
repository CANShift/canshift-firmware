#pragma once

#include "app_config.h"

#include <stdint.h>

namespace WorkshopMode {

inline constexpr uint8_t kNoResumeReason = 0;

#if APP_WORKSHOP_ENABLED

[[nodiscard]] bool boardSupported();

[[nodiscard]] bool offered();

[[nodiscard]] bool request();

[[nodiscard]] bool isEntering();

[[nodiscard]] bool isActive();

void settleEntry(bool entered);

void armResumeAfterUpdate();

void armResumeAfterConfigChange();

[[nodiscard]] uint8_t takeResumeRequest();

void resumeAfterReboot(uint8_t reason, bool imagePendingVerify);

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

inline void armResumeAfterConfigChange() {}

[[nodiscard]] inline uint8_t takeResumeRequest() {
    return kNoResumeReason;
}

inline void resumeAfterReboot(uint8_t, bool) {}

inline void leaveOnMotion(uint32_t) {}

#endif

} // namespace WorkshopMode
