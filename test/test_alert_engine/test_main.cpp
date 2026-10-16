#include "config/config_types.h"
#include "runtime/alert_engine.h"
#include "runtime/critical_alert_source.h"
#include "runtime/signal_store.h"

#include <Arduino.h>
#include <string.h>
#include <unity.h>

namespace {

using AlertEngine::AlertLevel;

constexpr uint32_t kFramePeriodMs = 100;
constexpr int kSettleFrames = 10;
constexpr int kDipFrames = 3;
constexpr int kFramesPastMinActive = 25;
constexpr float kHealthyBar = 4.0f;
constexpr float kNoPressureBar = 0.0f;
constexpr float kJustBelowCritBar = 0.9f;
constexpr float kInsideHysteresisBar = 1.01f;
constexpr float kUserFiresAtBar = 1.0f;

SignalStore::SignalValue s_snap[kSignalStoreMaxSignals];

void feedOilPressure(float bar) {
    mockAdvanceMillis(kFramePeriodMs);
    SignalStore::update(SignalIds::kOilPressBar, bar);
    SignalStore::snapshotAll(s_snap);
    AlertEngine::tick(s_snap);
}

void feedOilPressureFor(float bar, int frames) {
    for (int i = 0; i < frames; ++i)
        feedOilPressure(bar);
}

uint8_t oilPressureLevel() {
    return static_cast<uint8_t>(AlertEngine::getState().oilPressure);
}

uint8_t level(AlertLevel l) {
    return static_cast<uint8_t>(l);
}

void test_pressureLossFiresOnTheFirstFrameBelowTheThreshold() {
    feedOilPressureFor(kHealthyBar, kSettleFrames);
    TEST_ASSERT_EQUAL_UINT8(level(AlertLevel::NORMAL), oilPressureLevel());
    feedOilPressure(kNoPressureBar);
    TEST_ASSERT_EQUAL_UINT8(level(AlertLevel::CRITICAL), oilPressureLevel());
}

void test_shortDipFiresAndIsHeldForTheMinimumActiveTime() {
    feedOilPressureFor(kHealthyBar, kSettleFrames);
    feedOilPressureFor(kNoPressureBar, kDipFrames);
    TEST_ASSERT_EQUAL_UINT8(level(AlertLevel::CRITICAL), oilPressureLevel());
    feedOilPressure(kHealthyBar);
    TEST_ASSERT_EQUAL_UINT8(level(AlertLevel::CRITICAL), oilPressureLevel());
    feedOilPressureFor(kHealthyBar, kFramesPastMinActive);
    TEST_ASSERT_EQUAL_UINT8(level(AlertLevel::NORMAL), oilPressureLevel());
}

void test_noiseInsideTheHysteresisBandKeepsTheAlertRaised() {
    feedOilPressureFor(kHealthyBar, kSettleFrames);
    feedOilPressure(kJustBelowCritBar);
    feedOilPressureFor(kInsideHysteresisBar, kFramesPastMinActive);
    TEST_ASSERT_EQUAL_UINT8(level(AlertLevel::CRITICAL), oilPressureLevel());
}

void test_userCriticalAlertFiresOnTheFirstFrameBelow() {
    CfgCriticalAlert cfg = {};
    cfg.armed = true;
    strlcpy(cfg.signalId, "oil_press_bar", sizeof(cfg.signalId));
    cfg.firesAt = kUserFiresAtBar;
    cfg.below = true;
    CriticalAlertSource::init(cfg);
    feedOilPressureFor(kHealthyBar, kSettleFrames);
    feedOilPressure(kNoPressureBar);
    bool sensorLost = true;
    const AlertLevel user = CriticalAlertSource::step(s_snap, millis(), &sensorLost);
    TEST_ASSERT_FALSE(sensorLost);
    TEST_ASSERT_EQUAL_UINT8(level(AlertLevel::CRITICAL), level(user));
}

} // namespace

void setUp() {
    mockSetMillis(0);
    SignalStore::init();
    AlertEngine::init();
}

void tearDown() {}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_pressureLossFiresOnTheFirstFrameBelowTheThreshold);
    RUN_TEST(test_shortDipFiresAndIsHeldForTheMinimumActiveTime);
    RUN_TEST(test_noiseInsideTheHysteresisBandKeepsTheAlertRaised);
    RUN_TEST(test_userCriticalAlertFiresOnTheFirstFrameBelow);
    return UNITY_END();
}
