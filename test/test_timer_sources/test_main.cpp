#include "ui/widgets/timer_sources.h"

#include <string.h>
#include <unity.h>

namespace {

TimerSources::Inputs trackRunning() {
    return {12345, 2, 98420, 97190, 96080, 4, -420, true};
}

TimerSources::Inputs stopwatchOnly() {
    return {65432, 3, 0, 0, 0, 0, 0, false};
}

const char *rendered(CfgTimerSource source, const TimerSources::Inputs &in, char *buf, size_t cap) {
    TimerSources::render(source, in, buf, cap);
    return buf;
}

} // namespace

void setUp() {}
void tearDown() {}

void test_lapTimeFormatsAsMinutesSecondsHundredths() {
    char buf[TimerSources::kTextCapacity];
    const TimerSources::Inputs in = trackRunning();
    TEST_ASSERT_EQUAL_STRING("1:38.42", rendered(CfgTimerSource::Lap, in, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("1:36.08", rendered(CfgTimerSource::Best, in, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("1:37.19", rendered(CfgTimerSource::Last, in, buf, sizeof(buf)));
}

void test_deltaCarriesItsSign() {
    char buf[TimerSources::kTextCapacity];
    TimerSources::Inputs in = trackRunning();
    TEST_ASSERT_EQUAL_STRING("-0.42", rendered(CfgTimerSource::Delta, in, buf, sizeof(buf)));
    in.deltaMs = 310;
    TEST_ASSERT_EQUAL_STRING("+0.31", rendered(CfgTimerSource::Delta, in, buf, sizeof(buf)));
    in.deltaMs = 0;
    TEST_ASSERT_EQUAL_STRING("+0.00", rendered(CfgTimerSource::Delta, in, buf, sizeof(buf)));
}

void test_deltaIsBlankWithoutTrackTelemetry() {
    char buf[TimerSources::kTextCapacity];
    const TimerSources::Inputs in = stopwatchOnly();
    TEST_ASSERT_EQUAL_STRING("--", rendered(CfgTimerSource::Delta, in, buf, sizeof(buf)));
}

void test_lapFallsBackToTheStopwatchWhenTrackIsIdle() {
    char buf[TimerSources::kTextCapacity];
    const TimerSources::Inputs in = stopwatchOnly();
    TEST_ASSERT_EQUAL_STRING("1:05.43", rendered(CfgTimerSource::Lap, in, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("1:05.43", rendered(CfgTimerSource::Elapsed, in, buf, sizeof(buf)));
}

void test_unsetLapTimesShowAPlaceholderRatherThanZero() {
    char buf[TimerSources::kTextCapacity];
    const TimerSources::Inputs in = stopwatchOnly();
    TEST_ASSERT_EQUAL_STRING("--:--", rendered(CfgTimerSource::Best, in, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("--:--", rendered(CfgTimerSource::Last, in, buf, sizeof(buf)));
}

void test_lapCountPrefersTrackTelemetry() {
    char buf[TimerSources::kTextCapacity];
    TEST_ASSERT_EQUAL_STRING("4",
                             rendered(CfgTimerSource::LapCount, trackRunning(), buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("3",
                             rendered(CfgTimerSource::LapCount, stopwatchOnly(), buf, sizeof(buf)));
}

void test_deltaClampsRatherThanOverflowingItsBuffer() {
    char buf[TimerSources::kTextCapacity];
    TimerSources::Inputs in = trackRunning();
    in.deltaMs = 2000000000;
    TimerSources::render(CfgTimerSource::Delta, in, buf, sizeof(buf));
    TEST_ASSERT_TRUE(strlen(buf) < TimerSources::kTextCapacity);
    in.deltaMs = -2000000000;
    TimerSources::render(CfgTimerSource::Delta, in, buf, sizeof(buf));
    TEST_ASSERT_TRUE(strlen(buf) < TimerSources::kTextCapacity);
    TEST_ASSERT_EQUAL_CHAR('-', buf[0]);
}

void test_onlyTheStopwatchIsInteractive() {
    TEST_ASSERT_TRUE(TimerSources::isInteractive(CfgTimerSource::Elapsed));
    TEST_ASSERT_FALSE(TimerSources::isInteractive(CfgTimerSource::Lap));
    TEST_ASSERT_FALSE(TimerSources::isInteractive(CfgTimerSource::Delta));
}

void test_everySourceCarriesItsKicker() {
    TEST_ASSERT_EQUAL_STRING("TIMER", TimerSources::kicker(CfgTimerSource::Elapsed));
    TEST_ASSERT_EQUAL_STRING("LAP", TimerSources::kicker(CfgTimerSource::Lap));
    TEST_ASSERT_EQUAL_STRING("BEST", TimerSources::kicker(CfgTimerSource::Best));
    TEST_ASSERT_EQUAL_STRING("LAST", TimerSources::kicker(CfgTimerSource::Last));
    TEST_ASSERT_EQUAL_STRING("LAPS", TimerSources::kicker(CfgTimerSource::LapCount));
    TEST_ASSERT_EQUAL_STRING("DELTA", TimerSources::kicker(CfgTimerSource::Delta));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_lapTimeFormatsAsMinutesSecondsHundredths);
    RUN_TEST(test_deltaCarriesItsSign);
    RUN_TEST(test_deltaIsBlankWithoutTrackTelemetry);
    RUN_TEST(test_lapFallsBackToTheStopwatchWhenTrackIsIdle);
    RUN_TEST(test_unsetLapTimesShowAPlaceholderRatherThanZero);
    RUN_TEST(test_lapCountPrefersTrackTelemetry);
    RUN_TEST(test_deltaClampsRatherThanOverflowingItsBuffer);
    RUN_TEST(test_onlyTheStopwatchIsInteractive);
    RUN_TEST(test_everySourceCarriesItsKicker);
    return UNITY_END();
}
