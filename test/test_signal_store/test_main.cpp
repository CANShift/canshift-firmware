#include "runtime/signal_store.h"

#include <Arduino.h>
#include <cmath>
#include <unity.h>

namespace {

constexpr SignalId kTestSignalId = SignalIds::kRpm;
constexpr SignalId kUnwrittenSignalId = SignalIds::kOilPressBar;
constexpr float kTestSignalValue = 1234.0f;
constexpr float kReadDefault = -1.0f;
constexpr float kEpsilon = 1e-4f;
constexpr uint32_t kPastTimeoutMs = kSignalDefaultTimeoutMs + 5;
constexpr int kToggleCycles = 10;
constexpr float kRampStartRpm = 3000.0f;
constexpr float kRampStepRpm = 500.0f;
constexpr int kRampFrames = 8;
constexpr SignalId kSlowSignalId = SignalIds::kCoolantTempC;
constexpr float kCoolantBefore = 90.0f;
constexpr float kCoolantAfter = 100.0f;
constexpr uint32_t kFrameMs = 100;
constexpr uint32_t kSettleMs = 3000;
constexpr float kSettledEpsilon = 0.01f;

}

void setUp() {
    mockSetMillis(0);
    SignalStore::init();
}

void tearDown() {}

void test_update_then_read_returnsValue_andValid() {
    SignalStore::update(kTestSignalId, kTestSignalValue);

    TEST_ASSERT_TRUE(SignalStore::isValid(kTestSignalId));
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, kTestSignalValue,
                             SignalStore::read(kTestSignalId, kReadDefault));
}

void test_read_unwritten_signal_isInvalid() {
    TEST_ASSERT_FALSE(SignalStore::isValid(kUnwrittenSignalId));
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, kReadDefault,
                             SignalStore::read(kUnwrittenSignalId, kReadDefault));
}

void test_timeout_invalidates_after_threshold() {
    SignalStore::update(kTestSignalId, kTestSignalValue);
    TEST_ASSERT_TRUE(SignalStore::isValid(kTestSignalId));

    mockAdvanceMillis(kPastTimeoutMs);
    SignalStore::checkTimeouts();

    TEST_ASSERT_FALSE(SignalStore::isValid(kTestSignalId));
}

void test_read_returnsTheLastFrameExactly() {
    SignalStore::update(kTestSignalId, 1.0f);
    SignalStore::update(kTestSignalId, 0.0f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, SignalStore::read(kTestSignalId, kReadDefault));
}

void test_repeated_toggles_stay_binary() {
    SignalStore::update(kTestSignalId, 0.7f);

    for (int i = 0; i < kToggleCycles; ++i) {
        const float target = (i % 2 == 0) ? 1.0f : 0.0f;
        SignalStore::update(kTestSignalId, target);
        TEST_ASSERT_EQUAL_FLOAT(target, SignalStore::read(kTestSignalId, kReadDefault));
    }
}

void test_snapshot_followsARampFrameByFrame() {
    SignalStore::SignalValue snap[kSignalStoreMaxSignals];
    for (int i = 0; i < kRampFrames; ++i) {
        const float rpm = kRampStartRpm + kRampStepRpm * static_cast<float>(i);
        SignalStore::update(kTestSignalId, rpm);
        SignalStore::snapshotAll(snap);
        TEST_ASSERT_EQUAL_FLOAT(rpm, snap[kTestSignalId].raw);
    }
}

void test_tryReadDisplay_reportsValidityAndValue() {
    float value = kReadDefault;
    TEST_ASSERT_FALSE(SignalStore::tryReadDisplay(kUnwrittenSignalId, &value));

    SignalStore::update(kTestSignalId, 1.0f);
    SignalStore::update(kTestSignalId, kTestSignalValue);
    TEST_ASSERT_TRUE(SignalStore::tryReadDisplay(kTestSignalId, &value));
    TEST_ASSERT_EQUAL_FLOAT(kTestSignalValue, value);

    mockAdvanceMillis(kPastTimeoutMs);
    SignalStore::checkTimeouts();
    TEST_ASSERT_FALSE(SignalStore::tryReadDisplay(kTestSignalId, &value));
}

void test_tryReadDisplay_rejectsOutOfRangeIdAndNullOut() {
    float value = kReadDefault;
    TEST_ASSERT_FALSE(SignalStore::tryReadDisplay(SignalIds::kSignalCount, &value));
    TEST_ASSERT_EQUAL_FLOAT(kReadDefault, value);
    SignalStore::update(kTestSignalId, kTestSignalValue);
    TEST_ASSERT_FALSE(SignalStore::tryReadDisplay(kTestSignalId, nullptr));
}

void test_fastSignal_displaysTheDecodedValue() {
    SignalStore::update(kTestSignalId, kRampStartRpm);
    mockAdvanceMillis(kFrameMs);
    SignalStore::update(kTestSignalId, kTestSignalValue);
    TEST_ASSERT_EQUAL_FLOAT(kTestSignalValue, SignalStore::readDisplay(kTestSignalId));
}

void test_slowSignal_displayEasesWhileReadIsExact() {
    SignalStore::update(kSlowSignalId, kCoolantBefore);
    TEST_ASSERT_EQUAL_FLOAT(kCoolantBefore, SignalStore::readDisplay(kSlowSignalId));

    mockAdvanceMillis(kFrameMs);
    SignalStore::update(kSlowSignalId, kCoolantAfter);
    TEST_ASSERT_EQUAL_FLOAT(kCoolantAfter, SignalStore::read(kSlowSignalId));
    const float shown = SignalStore::readDisplay(kSlowSignalId);
    TEST_ASSERT_TRUE(shown > kCoolantBefore && shown < kCoolantAfter);

    float sent = 0.0f;
    TEST_ASSERT_TRUE(SignalStore::tryReadDisplay(kSlowSignalId, &sent));
    TEST_ASSERT_EQUAL_FLOAT(shown, sent);
}

void test_slowSignal_displaySettlesOnTheSample() {
    SignalStore::update(kSlowSignalId, kCoolantBefore);
    for (uint32_t elapsed = 0; elapsed < kSettleMs; elapsed += kFrameMs) {
        mockAdvanceMillis(kFrameMs);
        SignalStore::update(kSlowSignalId, kCoolantAfter);
    }
    TEST_ASSERT_FLOAT_WITHIN(kSettledEpsilon, kCoolantAfter,
                             SignalStore::readDisplay(kSlowSignalId));
}

void test_update_ignoresNonFiniteValues() {
    SignalStore::update(kTestSignalId, NAN);
    TEST_ASSERT_FALSE(SignalStore::isValid(kTestSignalId));

    SignalStore::update(kTestSignalId, kTestSignalValue);
    SignalStore::update(kTestSignalId, INFINITY);
    SignalStore::update(kTestSignalId, -INFINITY);
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, kTestSignalValue, SignalStore::read(kTestSignalId));
    SignalStore::SignalValue snap[kSignalStoreMaxSignals];
    SignalStore::snapshotAll(snap);
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, kTestSignalValue, snap[kTestSignalId].raw);
}

void test_read_returnsDefaultWhenInvalidOrOutOfRange() {
    TEST_ASSERT_EQUAL_FLOAT(kReadDefault, SignalStore::read(kUnwrittenSignalId, kReadDefault));
    TEST_ASSERT_EQUAL_FLOAT(kReadDefault, SignalStore::read(SignalIds::kSignalCount, kReadDefault));
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_update_then_read_returnsValue_andValid);
    RUN_TEST(test_read_unwritten_signal_isInvalid);
    RUN_TEST(test_timeout_invalidates_after_threshold);
    RUN_TEST(test_read_returnsTheLastFrameExactly);
    RUN_TEST(test_repeated_toggles_stay_binary);
    RUN_TEST(test_snapshot_followsARampFrameByFrame);
    RUN_TEST(test_tryReadDisplay_reportsValidityAndValue);
    RUN_TEST(test_tryReadDisplay_rejectsOutOfRangeIdAndNullOut);
    RUN_TEST(test_fastSignal_displaysTheDecodedValue);
    RUN_TEST(test_slowSignal_displayEasesWhileReadIsExact);
    RUN_TEST(test_slowSignal_displaySettlesOnTheSample);
    RUN_TEST(test_update_ignoresNonFiniteValues);
    RUN_TEST(test_read_returnsDefaultWhenInvalidOrOutOfRange);
    return UNITY_END();
}
