#include "ui/widgets/timer_sources.h"

#include <string.h>
#include <unity.h>

namespace {

constexpr uint32_t kStartMs = 10000;

LaunchTimer launchTimer(uint8_t state, uint32_t sprintMs, uint32_t stripMs) {
    LaunchTimer timer = {};
    timer.state = state;
    timer.start_ms = kStartMs;
    timer.sprint_ms = sprintMs;
    timer.strip_ms = stripMs;
    return timer;
}

TimerSources::Inputs withLaunch(const LaunchTimer &timer, uint32_t nowMs) {
    TimerSources::Inputs in =
        TimerSources::select(TimerService::Snapshot{}, TrackStore::State{}, false);
    in.launch = TimerSources::launchReading(timer, nowMs);
    return in;
}

char s_buf[TimerSources::kTextCapacity];

bool render(CfgTimerSource source, const TimerSources::Inputs &in) {
    return TimerSources::render(source, in, s_buf, sizeof(s_buf));
}

}

void setUp() {}
void tearDown() {}

void test_noRun_readsAsNoValue() {
    TEST_ASSERT_FALSE(render(CfgTimerSource::LaunchSprint, withLaunch(LaunchTimer{}, 0)));
    TEST_ASSERT_EQUAL_STRING("--:--", s_buf);
}

void test_running_showsTheLiveElapsedTime() {
    const TimerSources::Inputs in = withLaunch(launchTimer(kLaunchRunning, 0, 0), kStartMs + 3450);
    TEST_ASSERT_TRUE(render(CfgTimerSource::LaunchSprint, in));
    TEST_ASSERT_EQUAL_STRING("0:03.45", s_buf);
    TEST_ASSERT_TRUE(render(CfgTimerSource::LaunchStrip, in));
    TEST_ASSERT_EQUAL_STRING("0:03.45", s_buf);
}

void test_running_freezesTheSprintOnceReached() {
    const TimerSources::Inputs in =
        withLaunch(launchTimer(kLaunchRunning, 5420, 0), kStartMs + 9000);
    TEST_ASSERT_TRUE(render(CfgTimerSource::LaunchSprint, in));
    TEST_ASSERT_EQUAL_STRING("0:05.42", s_buf);
    TEST_ASSERT_TRUE(render(CfgTimerSource::LaunchStrip, in));
    TEST_ASSERT_EQUAL_STRING("0:09.00", s_buf);
}

void test_done_keepsTheResultsWhileReArmed() {
    const TimerSources::Inputs in = withLaunch(launchTimer(kLaunchArmed, 5420, 13870), 99999);
    TEST_ASSERT_TRUE(render(CfgTimerSource::LaunchStrip, in));
    TEST_ASSERT_EQUAL_STRING("0:13.87", s_buf);
}

void test_done_withoutAStrip_readsAsNoValue() {
    const TimerSources::Inputs in = withLaunch(launchTimer(kLaunchDone, 5420, 0), 99999);
    TEST_ASSERT_FALSE(render(CfgTimerSource::LaunchStrip, in));
}

void test_kickers_followTheUnitSystem() {
    TEST_ASSERT_EQUAL_STRING("0-100", TimerSources::kicker(CfgTimerSource::LaunchSprint, false));
    TEST_ASSERT_EQUAL_STRING("0-60", TimerSources::kicker(CfgTimerSource::LaunchSprint, true));
    TEST_ASSERT_EQUAL_STRING("400 M", TimerSources::kicker(CfgTimerSource::LaunchStrip, false));
    TEST_ASSERT_EQUAL_STRING("1/4 MI", TimerSources::kicker(CfgTimerSource::LaunchStrip, true));
}

void test_launchSources_areReadouts() {
    TEST_ASSERT_FALSE(TimerSources::isInteractive(CfgTimerSource::LaunchSprint));
    TEST_ASSERT_FALSE(TimerSources::isInteractive(CfgTimerSource::LaunchStrip));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_noRun_readsAsNoValue);
    RUN_TEST(test_running_showsTheLiveElapsedTime);
    RUN_TEST(test_running_freezesTheSprintOnceReached);
    RUN_TEST(test_done_keepsTheResultsWhileReArmed);
    RUN_TEST(test_done_withoutAStrip_readsAsNoValue);
    RUN_TEST(test_kickers_followTheUnitSystem);
    RUN_TEST(test_launchSources_areReadouts);
    return UNITY_END();
}
