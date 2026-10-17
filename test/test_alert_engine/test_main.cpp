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

CfgCriticalAlert configuredAlert(const char *signal, float firesAt, bool below) {
    CfgCriticalAlert cfg = {};
    cfg.armed = true;
    strlcpy(cfg.signalId, signal, sizeof(cfg.signalId));
    cfg.firesAt = firesAt;
    cfg.below = below;
    return cfg;
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
    const CfgCriticalAlert cfg = configuredAlert("oil_press_bar", kUserFiresAtBar, true);
    CriticalAlertSource::init(&cfg, 1);
    feedOilPressureFor(kHealthyBar, kSettleFrames);
    feedOilPressure(kNoPressureBar);
    bool sensorLost = true;
    const AlertLevel user = CriticalAlertSource::step(0, s_snap, millis(), &sensorLost);
    TEST_ASSERT_FALSE(sensorLost);
    TEST_ASSERT_EQUAL_UINT8(level(AlertLevel::CRITICAL), level(user));
}

void test_everyConfiguredAlertIsJudgedOnItsOwn() {
    const CfgCriticalAlert alerts[] = {configuredAlert("oil_press_bar", kUserFiresAtBar, true),
                                       configuredAlert("oil_press_bar", 8.0f, false)};
    CriticalAlertSource::init(alerts, 2);
    TEST_ASSERT_EQUAL_size_t(2, CriticalAlertSource::count());
    feedOilPressureFor(kHealthyBar, kSettleFrames);
    feedOilPressure(kNoPressureBar);
    const AlertEngine::AlertState state = AlertEngine::getState();
    TEST_ASSERT_EQUAL_UINT8(level(AlertLevel::CRITICAL), level(state.userCritical[0]));
    TEST_ASSERT_EQUAL_UINT8(level(AlertLevel::NORMAL), level(state.userCritical[1]));
    TEST_ASSERT_EQUAL_UINT8(level(AlertLevel::CRITICAL), level(state.global));
}

void test_anUnreadableEntryKeepsTheIndexOfTheNextOne() {
    const CfgCriticalAlert alerts[] = {configuredAlert("not_a_dash_signal", 1.0f, true),
                                       configuredAlert("oil_press_bar", kUserFiresAtBar, true)};
    CriticalAlertSource::init(alerts, 2);
    TEST_ASSERT_FALSE(CriticalAlertSource::armed(0).armed);
    TEST_ASSERT_TRUE(CriticalAlertSource::armed(1).armed);
    feedOilPressureFor(kHealthyBar, kSettleFrames);
    feedOilPressure(kNoPressureBar);
    TEST_ASSERT_EQUAL_UINT8(level(AlertLevel::NORMAL),
                            level(AlertEngine::getState().userCritical[0]));
    TEST_ASSERT_EQUAL_UINT8(level(AlertLevel::CRITICAL),
                            level(AlertEngine::getState().userCritical[1]));
}

void test_initClampsTheListToTheCap() {
    CfgCriticalAlert alerts[kConfigMaxCriticalAlerts + 1];
    for (CfgCriticalAlert &alert : alerts)
        alert = configuredAlert("oil_press_bar", kUserFiresAtBar, true);
    CriticalAlertSource::init(alerts, kConfigMaxCriticalAlerts + 1);
    TEST_ASSERT_EQUAL_size_t(kConfigMaxCriticalAlerts, CriticalAlertSource::count());
    TEST_ASSERT_FALSE(CriticalAlertSource::armed(kConfigMaxCriticalAlerts).armed);
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
    RUN_TEST(test_everyConfiguredAlertIsJudgedOnItsOwn);
    RUN_TEST(test_anUnreadableEntryKeepsTheIndexOfTheNextOne);
    RUN_TEST(test_initClampsTheListToTheCap);
    return UNITY_END();
}
