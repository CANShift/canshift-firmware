#pragma once

#include <atomic>
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

} // namespace PendingActions
