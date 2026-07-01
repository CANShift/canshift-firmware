#pragma once

#include "config/config_types.h"

#include <string.h>

#include <atomic>
#include <cstddef>
#include <stdint.h>

namespace PendingActions {

inline std::atomic<bool> dayNightToggle{false};

inline std::atomic<int8_t> dayNightSet{-1};

inline std::atomic<bool> touchCalibrate{false};

inline std::atomic<bool> touchCalibrationReset{false};

inline std::atomic<uint32_t> blePasskeyShow{0};

inline std::atomic<bool> blePasskeyHide{false};

inline std::atomic<bool> burnOverlayShow{false};

inline std::atomic<int8_t> burnOverlayShowError{-1};

inline std::atomic<uint32_t> otaOverlayShowSize{0};

inline std::atomic<bool> otaOverlayHide{false};

inline std::atomic<bool> navPagePending{false};

inline char navPageId[CFG_MAX_ID_LEN] = {};

inline bool takeDayNightToggle() {
    return dayNightToggle.exchange(false, std::memory_order_relaxed);
}

inline int8_t takeDayNightSet() {
    return dayNightSet.exchange(-1, std::memory_order_relaxed);
}

inline bool takeTouchCalibrate() {
    return touchCalibrate.exchange(false, std::memory_order_relaxed);
}

inline bool takeTouchCalibrationReset() {
    return touchCalibrationReset.exchange(false, std::memory_order_relaxed);
}

inline uint32_t takeBlePasskeyShow() {
    return blePasskeyShow.exchange(0, std::memory_order_relaxed);
}

inline bool takeBlePasskeyHide() {
    return blePasskeyHide.exchange(false, std::memory_order_relaxed);
}

inline bool takeBurnOverlayShow() {
    return burnOverlayShow.exchange(false, std::memory_order_relaxed);
}

inline int8_t takeBurnOverlayShowError() {
    return burnOverlayShowError.exchange(-1, std::memory_order_relaxed);
}

inline uint32_t takeOtaOverlayShowSize() {
    return otaOverlayShowSize.exchange(0, std::memory_order_relaxed);
}

inline bool takeOtaOverlayHide() {
    return otaOverlayHide.exchange(false, std::memory_order_relaxed);
}

// Single slot, last-write-wins. Release/acquire pairs the id write with the
// flag so the UI task never observes the flag before the buffer.
inline void requestNavPage(const char *pageId) {
    strlcpy(navPageId, pageId, sizeof(navPageId));
    navPagePending.store(true, std::memory_order_release);
}

inline bool takeNavPage(char *out, size_t outLen) {
    if (!navPagePending.exchange(false, std::memory_order_acquire))
        return false;
    strlcpy(out, navPageId, outLen);
    return true;
}

} // namespace PendingActions
