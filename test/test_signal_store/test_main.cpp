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
                             SignalStore::readRaw(kTestSignalId, kReadDefault));
}

void test_get_unwritten_signal_isInvalid() {
    TEST_ASSERT_FALSE(SignalStore::isValid(kUnwrittenSignalId));
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, kReadDefault,
                             SignalStore::readRaw(kUnwrittenSignalId, kReadDefault));
}

void test_timeout_invalidates_after_threshold() {
    SignalStore::update(kTestSignalId, kTestSignalValue);
    TEST_ASSERT_TRUE(SignalStore::isValid(kTestSignalId));

    mockAdvanceMillis(kPastTimeoutMs);
    SignalStore::checkTimeouts();

    TEST_ASSERT_FALSE(SignalStore::isValid(kTestSignalId));
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_set_then_get_returnsValue_andValid);
    RUN_TEST(test_get_unwritten_signal_isInvalid);
    RUN_TEST(test_timeout_invalidates_after_threshold);
    return UNITY_END();
}
