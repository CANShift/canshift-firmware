// test_main.cpp — Unity tests for the thread-safe SignalStore.
//
// SignalStore relies on `millis()` for timeout detection. The host shim's
// `mockSetMillis` / `mockAdvanceMillis` helpers make this deterministic.
//
// The Arduino + FreeRTOS shims reduce the mutex to a no-op in single-threaded
// host runs, so these tests exercise the value lifecycle (write, read,
// timeout) without spinning a real scheduler.

#include "runtime/signal_store.h"

#include <Arduino.h>
#include <unity.h>

namespace {

constexpr SignalId kTestSignalId = SignalIds::RPM;
constexpr SignalId kUnwrittenSignalId = SignalIds::OIL_PRESS_BAR;
constexpr float kTestSignalValue = 1234.0f;
constexpr float kReadDefault = -1.0f;
constexpr float kEpsilon = 1e-4f;

// 5 ms past SIGNAL_DEFAULT_TIMEOUT_MS — guaranteed to exceed the staleness
// window without sitting exactly on the boundary.
constexpr uint32_t kPastTimeoutMs = SIGNAL_DEFAULT_TIMEOUT_MS + 5;

} // namespace

void setUp() {
    // Reset the millis counter and rebuild the store mutex so each test
    // starts from a known baseline. SignalStore::init() also clears all
    // slots back to invalid.
    mockSetMillis(0);
    SignalStore::init();
}

void tearDown() {}

void test_set_then_get_returnsValue_andValid() {
    SignalStore::update(kTestSignalId, kTestSignalValue);

    TEST_ASSERT_TRUE(SignalStore::isValid(kTestSignalId));
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, kTestSignalValue,
                             SignalStore::read(kTestSignalId, kReadDefault));
}

void test_get_unwritten_signal_isInvalid() {
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

// Regression: synthetic toggle writes via SignalStore::set must bypass EMA
// so read() returns exactly the written value (#1285). Anything fuzzier
// than exact equality re-creates the button-stuck symptom.
void test_set_writes_exact_value() {
    SignalStore::set(kTestSignalId, 1.0f);
    TEST_ASSERT_TRUE(SignalStore::isValid(kTestSignalId));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, SignalStore::read(kTestSignalId, kReadDefault));

    SignalStore::set(kTestSignalId, 0.0f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, SignalStore::read(kTestSignalId, kReadDefault));
}

// Regression: 10 alternating set() calls — read() must report exactly 0 or
// 1 on every cycle, even though the signal already has a previous smoothed
// value sitting in the slot from earlier updates.
void test_set_repeated_toggles_stay_binary() {
    // Seed with a non-binary smoothed value so the bug from #1285 would
    // surface immediately if set() ever fell back to the EMA path.
    SignalStore::update(kTestSignalId, 0.7f);

    for (int i = 0; i < 10; ++i) {
        const float target = (i % 2 == 0) ? 1.0f : 0.0f;
        SignalStore::set(kTestSignalId, target);
        TEST_ASSERT_EQUAL_FLOAT(target, SignalStore::read(kTestSignalId, kReadDefault));
    }
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_set_then_get_returnsValue_andValid);
    RUN_TEST(test_get_unwritten_signal_isInvalid);
    RUN_TEST(test_timeout_invalidates_after_threshold);
    RUN_TEST(test_set_writes_exact_value);
    RUN_TEST(test_set_repeated_toggles_stay_binary);
    return UNITY_END();
}
