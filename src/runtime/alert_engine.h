#pragma once
// alert_engine.h — Warning and alert state machine
//
// Monitors signal values for critical conditions and drives:
//   1. Rev limiter flash effect (full-screen red overlay, flashing)
//   2. Temperature warning indicators
//   3. Oil pressure critical alert
//   4. MIL (check engine) indicator state
//
// Alert states are prioritized: CRITICAL > WARNING > CAUTION > NORMAL
// The UI queries alert state to decide which overlay/color to show.

#include <stdint.h>
#include <stdbool.h>

namespace AlertEngine {

enum class AlertLevel : uint8_t {
    NORMAL = 0,
    CAUTION = 1, // Yellow — approaching threshold
    WARNING = 2, // Orange — at threshold
    CRITICAL = 3 // Red — over threshold, requires immediate attention
};

struct AlertState {
    AlertLevel revLimiter;
    bool revLimiterFlashActive; // True when flash effect should be shown
    AlertLevel coolantTemp;
    AlertLevel oilTemp;
    AlertLevel oilPressure;
    bool milActive;
    AlertLevel batteryVoltage;
    AlertLevel global; // Highest of all alert levels
};

/**
     * Initialize alert engine.
     * Call from BootSequence::run().
     */
void init();

/**
     * Update alert states from current signal values.
     * Call from UI task at every render tick.
     */
void tick();

/**
     * Get current alert state snapshot (thread-safe copy).
     */
AlertState getState();

/**
     * Return true if the rev limiter flash overlay should be shown right now.
     * This alternates at ALERT_REVLIMIT_FLASH_HZ frequency.
     */
bool isRevLimiterFlashOn();

/**
     * Return the LVGL color to use for the rev limiter overlay.
     * Returns transparent color when flash is off.
     */
uint32_t getRevLimiterOverlayColor();

} // namespace AlertEngine
