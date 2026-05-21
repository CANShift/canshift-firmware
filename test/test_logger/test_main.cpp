// test_main.cpp — Pins down the Logger::lockUart recursive-mutex contract.
//
// Background (issue #1037): PR #1027 swapped Logger's binary mutex for
// xSemaphoreCreateRecursiveMutex so a task holding lockUart() can still
// LOG_* without deadlocking. The contract is documented but was not tested,
// so a future refactor that downgrades Take/GiveRecursive back to the binary
// API would silently re-introduce the deadlock.
//
// These tests exercise the recursive path against the native FreeRTOS shim
// (test/native/shim/freertos/semphr.h) which counts Take/Give depth. If
// logger.cpp ever calls the wrong primitive, the depth counter goes negative
// (or never reaches 2 during nested emit) and the assertions trip.

// Relative include bypasses the test/native/shim/diag/logger.h no-op shim
// that other native tests rely on — this one is specifically exercising the
// production Logger.
#include "../../src/diag/logger.h"

#include <Arduino.h>
#include <unity.h>
#include <freertos/semphr.h>

#include <string.h>

namespace {

bool jsonLineContains(const char *substr) {
    return strstr(canshift_test::serialCaptureBuffer(), substr) != nullptr;
}

} // namespace

void setUp() {
    canshift_test::serialCaptureReset();
    canshift_test::recursiveMutexDepth() = 0;
    Logger::init();
    // init() takes no depth, but a stale depth from a prior failing test
    // would mask a regression, so reset explicitly above.
}

void tearDown() {
    // Every test must leave the mutex unlocked. If a test forgets, this
    // catches the leak before the next test inherits the depth and silently
    // passes.
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, canshift_test::recursiveMutexDepth(),
                                    "Logger mutex left held across test boundary");
}

// Baseline: a plain LOG_* emits one JSON-line envelope on the serial mock and
// leaves the mutex unlocked. Anchors the rest of the suite — if this fails,
// the recursive-mutex test result is meaningless.
void test_log_error_emits_envelope() {
    Logger::emit('E', "TST", "baseline %d", 1);
    TEST_ASSERT_TRUE(jsonLineContains("\"lvl\":\"E\""));
    TEST_ASSERT_TRUE(jsonLineContains("\"tag\":\"TST\""));
    TEST_ASSERT_TRUE(jsonLineContains("\"msg\":\"baseline 1\""));
    TEST_ASSERT_EQUAL_INT32(0, canshift_test::recursiveMutexDepth());
}

// The contract under test: a task holding Logger::lockUart() can issue a
// nested LOG_ERROR without deadlocking. The recursive mutex must reach depth
// 2 during emit() and return to depth 1 after, with the outer caller still
// holding its lock. Mirrors the assert-handler scenario from F-HI-6.
void test_reentrant_emit_under_held_lock() {
    TEST_ASSERT_TRUE(Logger::lockUart(pdMS_TO_TICKS(50)));
    TEST_ASSERT_EQUAL_INT32_MESSAGE(1, canshift_test::recursiveMutexDepth(),
                                    "lockUart should bring depth to 1");

    // Nested LOG_ERROR — must succeed (depth 1 -> 2 -> 1) and emit the line.
    Logger::emit('E', "REE", "nested under held lock");

    TEST_ASSERT_EQUAL_INT32_MESSAGE(
        1, canshift_test::recursiveMutexDepth(),
        "After nested emit returns, the outer lockUart hold must still be 1");
    TEST_ASSERT_TRUE_MESSAGE(jsonLineContains("\"msg\":\"nested under held lock\""),
                             "Nested LOG_ERROR was silently dropped — recursive contract broken");

    Logger::unlockUart();
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, canshift_test::recursiveMutexDepth(),
                                    "unlockUart should bring depth back to 0");
}

// Deep nesting: lock + emit + emit should still balance. Catches a regression
// where emit() forgets to Give on one path (e.g. the early-return on dropped
// lines after a refactor).
void test_two_nested_emits_under_held_lock() {
    TEST_ASSERT_TRUE(Logger::lockUart(pdMS_TO_TICKS(50)));
    Logger::emit('W', "REE", "one");
    Logger::emit('I', "REE", "two");
    TEST_ASSERT_EQUAL_INT32(1, canshift_test::recursiveMutexDepth());
    Logger::unlockUart();
    TEST_ASSERT_EQUAL_INT32(0, canshift_test::recursiveMutexDepth());
    TEST_ASSERT_TRUE(jsonLineContains("\"msg\":\"one\""));
    TEST_ASSERT_TRUE(jsonLineContains("\"msg\":\"two\""));
}

// Balanced lock/unlock outside emit must round-trip — a regression here would
// indicate lockUart / unlockUart drift apart in API.
void test_lock_unlock_round_trip() {
    TEST_ASSERT_TRUE(Logger::lockUart(pdMS_TO_TICKS(50)));
    TEST_ASSERT_EQUAL_INT32(1, canshift_test::recursiveMutexDepth());
    Logger::unlockUart();
    TEST_ASSERT_EQUAL_INT32(0, canshift_test::recursiveMutexDepth());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_log_error_emits_envelope);
    RUN_TEST(test_reentrant_emit_under_held_lock);
    RUN_TEST(test_two_nested_emits_under_held_lock);
    RUN_TEST(test_lock_unlock_round_trip);
    return UNITY_END();
}
