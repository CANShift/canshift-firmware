#include "../../src/diag/logger.h"

#include <Arduino.h>
#include <unity.h>
#include <freertos/semphr.h>

#include <string.h>

namespace {

constexpr TickType_t kLockTimeout = pdMS_TO_TICKS(50);

bool jsonLineContains(const char *substr) {
    return strstr(canshift_test::serialCaptureBuffer(), substr) != nullptr;
}

int32_t mutexDepth() {
    return canshift_test::recursiveMutexDepth();
}

constexpr uint8_t kLine[] = "{\"tele\":1}\n";
constexpr size_t kLineLen = sizeof(kLine) - 1;

constexpr size_t kLongLineLen = 100;
constexpr int kRoomForOnePiece = 70;

void fillTxRingLeaving(int room) {
    const int free = Serial.availableForWrite();
    for (int i = room; i < free; ++i)
        Serial.write('x');
}

void fillTxRing() {
    fillTxRingLeaving(0);
}

void writeOnDrainedRing() {
    canshift_test::serialCaptureReset();
    TEST_ASSERT_TRUE(Logger::writeAll(kLine, kLineLen));
}

}

void setUp() {
    canshift_test::serialCaptureReset();
    canshift_test::recursiveMutexDepth() = 0;
    canshift_test::serialHostAttachedRef() = true;
    mockSetMillis(0);
    Logger::init();
}

void tearDown() {
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, mutexDepth(), "Logger mutex left held across test boundary");
}

void test_log_error_emits_envelope() {
    Logger::emit('E', "TST", "baseline %d", 1);
    TEST_ASSERT_TRUE(jsonLineContains("\"lvl\":\"E\""));
    TEST_ASSERT_TRUE(jsonLineContains("\"tag\":\"TST\""));
    TEST_ASSERT_TRUE(jsonLineContains("\"msg\":\"baseline 1\""));
    TEST_ASSERT_EQUAL_INT32(0, mutexDepth());
}

void test_reentrant_emit_under_held_lock() {
    {
        const Logger::UartLock lock(kLockTimeout);
        TEST_ASSERT_TRUE(lock.held());
        TEST_ASSERT_EQUAL_INT32_MESSAGE(1, mutexDepth(), "the lock should bring depth to 1");
        Logger::emit('E', "REE", "nested under held lock");
        TEST_ASSERT_EQUAL_INT32_MESSAGE(1, mutexDepth(),
                                        "after a nested emit the outer hold must still be 1");
        TEST_ASSERT_TRUE_MESSAGE(jsonLineContains("\"msg\":\"nested under held lock\""),
                                 "a nested LOG_ERROR was dropped");
    }
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, mutexDepth(), "leaving the scope should release the lock");
}

void test_two_nested_emits_under_held_lock() {
    {
        const Logger::UartLock lock(kLockTimeout);
        Logger::emit('W', "REE", "one");
        Logger::emit('I', "REE", "two");
        TEST_ASSERT_EQUAL_INT32(1, mutexDepth());
    }
    TEST_ASSERT_EQUAL_INT32(0, mutexDepth());
    TEST_ASSERT_TRUE(jsonLineContains("\"msg\":\"one\""));
    TEST_ASSERT_TRUE(jsonLineContains("\"msg\":\"two\""));
}

void test_lock_round_trip() {
    {
        const Logger::UartLock lock(kLockTimeout);
        TEST_ASSERT_TRUE(lock.held());
        TEST_ASSERT_EQUAL_INT32(1, mutexDepth());
    }
    TEST_ASSERT_EQUAL_INT32(0, mutexDepth());
}

void test_escapes_message_and_strips_tag() {
    Logger::emit('E', "T\"G\x01", "a\"b\\c\nd\t\x02");
    TEST_ASSERT_TRUE(jsonLineContains("\"tag\":\"TG\""));
    TEST_ASSERT_TRUE(jsonLineContains("\"msg\":\"a\\\"b\\\\c\\nd\\t\\u0002\""));
}

void test_unknown_level_falls_back_to_info() {
    Logger::emit('X', "TST", "level");
    TEST_ASSERT_TRUE(jsonLineContains("\"lvl\":\"I\""));
}

void test_write_without_host_still_fills_free_ring() {
    canshift_test::serialHostAttachedRef() = false;
    writeOnDrainedRing();
    TEST_ASSERT_TRUE(jsonLineContains("{\"tele\":1}"));
}

void test_write_without_host_drops_instead_of_waiting() {
    writeOnDrainedRing();
    canshift_test::serialHostAttachedRef() = false;
    fillTxRing();
    TEST_ASSERT_FALSE(Logger::writeAll(kLine, kLineLen));
    TEST_ASSERT_EQUAL_UINT32(0, millis());
}

void test_write_without_host_never_queues_a_partial_line() {
    canshift_test::serialHostAttachedRef() = false;
    writeOnDrainedRing();
    fillTxRingLeaving(kRoomForOnePiece);
    const size_t before = canshift_test::serialCaptureLen();
    uint8_t longLine[kLongLineLen];
    memset(longLine, 'y', sizeof(longLine));
    TEST_ASSERT_FALSE(Logger::writeAll(longLine, sizeof(longLine)));
    TEST_ASSERT_EQUAL_size_t(before, canshift_test::serialCaptureLen());
}

void test_attached_host_waits_for_drain_up_to_deadline() {
    writeOnDrainedRing();
    fillTxRing();
    TEST_ASSERT_FALSE(Logger::writeAll(kLine, kLineLen));
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(kUsbTxWriteTimeoutMs, millis());
}

void test_stalled_ring_drops_next_line_without_waiting() {
    writeOnDrainedRing();
    fillTxRing();
    TEST_ASSERT_FALSE(Logger::writeAll(kLine, kLineLen));
    mockSetMillis(0);
    TEST_ASSERT_FALSE(Logger::writeAll(kLine, kLineLen));
    TEST_ASSERT_EQUAL_UINT32(0, millis());
}

void test_drained_ring_resumes_waiting_writes() {
    writeOnDrainedRing();
    fillTxRing();
    TEST_ASSERT_FALSE(Logger::writeAll(kLine, kLineLen));
    writeOnDrainedRing();
    fillTxRing();
    mockSetMillis(0);
    TEST_ASSERT_FALSE(Logger::writeAll(kLine, kLineLen));
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(kUsbTxWriteTimeoutMs, millis());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_log_error_emits_envelope);
    RUN_TEST(test_reentrant_emit_under_held_lock);
    RUN_TEST(test_two_nested_emits_under_held_lock);
    RUN_TEST(test_lock_round_trip);
    RUN_TEST(test_escapes_message_and_strips_tag);
    RUN_TEST(test_unknown_level_falls_back_to_info);
    RUN_TEST(test_write_without_host_still_fills_free_ring);
    RUN_TEST(test_write_without_host_drops_instead_of_waiting);
    RUN_TEST(test_write_without_host_never_queues_a_partial_line);
    RUN_TEST(test_attached_host_waits_for_drain_up_to_deadline);
    RUN_TEST(test_stalled_ring_drops_next_line_without_waiting);
    RUN_TEST(test_drained_ring_resumes_waiting_writes);
    return UNITY_END();
}
