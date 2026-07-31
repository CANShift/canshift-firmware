#include "timer_core_rs.h"

#include <unity.h>

namespace {

constexpr int64_t SEC = 1000000;

TimerCoreState makeCore() {
    TimerCoreState core = {};
    timer_core_init_rs(&core);
    return core;
}

void test_start_onlyFromReset() {
    TimerCoreState core = makeCore();
    TEST_ASSERT_TRUE(timer_core_start_rs(&core, 0));
    TEST_ASSERT_FALSE(timer_core_start_rs(&core, SEC));
    TEST_ASSERT_EQUAL_UINT8(1, core.run_state);
    TEST_ASSERT_EQUAL_UINT16(1, core.session);
}

void test_elapsed_accumulatesAcrossPauseResume() {
    TimerCoreState core = makeCore();
    timer_core_start_rs(&core, 0);
    TEST_ASSERT_EQUAL_UINT32(5000, timer_core_elapsed_ms_rs(&core, 5 * SEC));
    TEST_ASSERT_TRUE(timer_core_pause_rs(&core, 5 * SEC));
    TEST_ASSERT_EQUAL_UINT32(5000, timer_core_elapsed_ms_rs(&core, 60 * SEC));
    TEST_ASSERT_TRUE(timer_core_resume_rs(&core, 60 * SEC));
    TEST_ASSERT_EQUAL_UINT32(7000, timer_core_elapsed_ms_rs(&core, 62 * SEC));
}

void test_lap_recordsSplitAndTotal() {
    TimerCoreState core = makeCore();
    timer_core_start_rs(&core, 0);

    TimerCoreLap lap = {};
    bool dropped = true;
    TEST_ASSERT_TRUE(timer_core_lap_rs(&core, 61 * SEC, &lap, &dropped));
    TEST_ASSERT_EQUAL_UINT16(1, lap.index);
    TEST_ASSERT_EQUAL_UINT32(61000, lap.lap_ms);
    TEST_ASSERT_EQUAL_UINT32(61000, lap.total_ms);
    TEST_ASSERT_FALSE(dropped);

    TEST_ASSERT_TRUE(timer_core_lap_rs(&core, 100 * SEC, &lap, &dropped));
    TEST_ASSERT_EQUAL_UINT16(2, lap.index);
    TEST_ASSERT_EQUAL_UINT32(39000, lap.lap_ms);
    TEST_ASSERT_EQUAL_UINT32(100000, lap.total_ms);
}

void test_lap_rejectedUnlessRunning() {
    TimerCoreState core = makeCore();
    TimerCoreLap lap = {};
    TEST_ASSERT_FALSE(timer_core_lap_rs(&core, 0, &lap, nullptr));
    timer_core_start_rs(&core, 0);
    timer_core_pause_rs(&core, SEC);
    TEST_ASSERT_FALSE(timer_core_lap_rs(&core, 2 * SEC, &lap, nullptr));
}

void test_reset_keepsPendingLaps() {
    TimerCoreState core = makeCore();
    timer_core_start_rs(&core, 0);
    timer_core_lap_rs(&core, 10 * SEC, nullptr, nullptr);
    TEST_ASSERT_TRUE(timer_core_reset_rs(&core));
    TEST_ASSERT_EQUAL_UINT8(0, core.run_state);
    TEST_ASSERT_EQUAL_UINT16(0, core.lap_count);
    TEST_ASSERT_EQUAL_UINT32(0, timer_core_elapsed_ms_rs(&core, 99 * SEC));
    TEST_ASSERT_EQUAL_UINT8(1, timer_core_pending_count_rs(&core));
    TEST_ASSERT_FALSE(timer_core_reset_rs(&core));
}

void test_pendingRing_flushesInOrder() {
    TimerCoreState core = makeCore();
    timer_core_start_rs(&core, 0);
    for (int64_t i = 1; i <= 3; i++) {
        timer_core_lap_rs(&core, i * SEC, nullptr, nullptr);
    }

    TimerCoreLap lap = {};
    for (uint16_t expected = 1; expected <= 3; expected++) {
        TEST_ASSERT_TRUE(timer_core_pop_pending_rs(&core, &lap));
        TEST_ASSERT_EQUAL_UINT16(expected, lap.index);
    }
    TEST_ASSERT_FALSE(timer_core_pop_pending_rs(&core, &lap));
}

void test_pendingRing_dropsOldestOnOverflow() {
    TimerCoreState core = makeCore();
    timer_core_start_rs(&core, 0);

    bool droppedSeen = false;
    for (int64_t i = 1; i <= TIMER_CORE_LAP_CAPACITY + 2; i++) {
        bool dropped = false;
        TEST_ASSERT_TRUE(timer_core_lap_rs(&core, i * SEC, nullptr, &dropped));
        droppedSeen |= dropped;
    }
    TEST_ASSERT_TRUE(droppedSeen);
    TEST_ASSERT_EQUAL_UINT8(TIMER_CORE_LAP_CAPACITY, timer_core_pending_count_rs(&core));

    TimerCoreLap lap = {};
    TEST_ASSERT_TRUE(timer_core_pop_pending_rs(&core, &lap));
    TEST_ASSERT_EQUAL_UINT16(3, lap.index);
}

void test_session_incrementsOnEachStart() {
    TimerCoreState core = makeCore();
    timer_core_start_rs(&core, 0);
    timer_core_reset_rs(&core);
    timer_core_start_rs(&core, 10 * SEC);

    TimerCoreLap lap = {};
    TEST_ASSERT_TRUE(timer_core_lap_rs(&core, 11 * SEC, &lap, nullptr));
    TEST_ASSERT_EQUAL_UINT16(2, lap.session);
    TEST_ASSERT_EQUAL_UINT16(1, lap.index);
}

void test_version_bumpsOnMutationsOnly() {
    TimerCoreState core = makeCore();
    const uint32_t idleVersion = core.version;
    timer_core_pause_rs(&core, 0);
    timer_core_resume_rs(&core, 0);
    timer_core_reset_rs(&core);
    TEST_ASSERT_EQUAL_UINT32(idleVersion, core.version);

    timer_core_start_rs(&core, 0);
    TEST_ASSERT_NOT_EQUAL_UINT32(idleVersion, core.version);
}

void test_nullPointers_areNoops() {
    TEST_ASSERT_FALSE(timer_core_start_rs(nullptr, 0));
    TEST_ASSERT_FALSE(timer_core_reset_rs(nullptr));
    TEST_ASSERT_EQUAL_UINT32(0, timer_core_elapsed_ms_rs(nullptr, SEC));
    TEST_ASSERT_EQUAL_UINT8(0, timer_core_pending_count_rs(nullptr));
}

} // namespace

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_start_onlyFromReset);
    RUN_TEST(test_elapsed_accumulatesAcrossPauseResume);
    RUN_TEST(test_lap_recordsSplitAndTotal);
    RUN_TEST(test_lap_rejectedUnlessRunning);
    RUN_TEST(test_reset_keepsPendingLaps);
    RUN_TEST(test_pendingRing_flushesInOrder);
    RUN_TEST(test_pendingRing_dropsOldestOnOverflow);
    RUN_TEST(test_session_incrementsOnEachStart);
    RUN_TEST(test_version_bumpsOnMutationsOnly);
    RUN_TEST(test_nullPointers_areNoops);
    return UNITY_END();
}
