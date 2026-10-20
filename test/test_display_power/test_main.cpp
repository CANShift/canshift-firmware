#include "alert_engine_rs.h"

#include <stdint.h>
#include <unity.h>

namespace {

constexpr uint32_t kDimAfterMs = 60000;
constexpr uint32_t kOffAfterMs = 600000;
constexpr uint32_t kTouchIdleAfterMs = 3000;
constexpr uint32_t kTouchReadIdleMs = 33;
constexpr uint32_t kTouchReadPanelOffMs = 100;

uint8_t idleState(uint32_t idleMs, bool alertHeld) {
    return alert_display_idle_state_rs(idleMs, kDimAfterMs, kOffAfterMs, alertHeld);
}

void test_quietPanelWalksLiveDimOff() {
    TEST_ASSERT_EQUAL_UINT8(kAlertDisplayIdleLive, idleState(kDimAfterMs - 1, false));
    TEST_ASSERT_EQUAL_UINT8(kAlertDisplayIdleDim, idleState(kDimAfterMs, false));
    TEST_ASSERT_EQUAL_UINT8(kAlertDisplayIdleOff, idleState(kOffAfterMs, false));
}

void test_criticalAlertKeepsTheDarkPanelLive() {
    TEST_ASSERT_EQUAL_UINT8(kAlertDisplayIdleLive, idleState(kOffAfterMs, true));
    TEST_ASSERT_EQUAL_UINT8(kAlertDisplayIdleLive, idleState(UINT32_MAX, true));
}

void test_recentTouchReadsAtTheLvglRate() {
    TEST_ASSERT_EQUAL_UINT32(kAlertTouchReadActiveMs,
                             alert_touch_read_period_ms_rs(kAlertDisplayIdleLive, 0));
    TEST_ASSERT_EQUAL_UINT32(
        kAlertTouchReadActiveMs,
        alert_touch_read_period_ms_rs(kAlertDisplayIdleLive, kTouchIdleAfterMs - 1));
}

void test_untouchedPanelReadsSlower() {
    TEST_ASSERT_EQUAL_UINT32(
        kTouchReadIdleMs, alert_touch_read_period_ms_rs(kAlertDisplayIdleLive, kTouchIdleAfterMs));
    TEST_ASSERT_EQUAL_UINT32(kTouchReadIdleMs,
                             alert_touch_read_period_ms_rs(kAlertDisplayIdleDim, UINT32_MAX));
}

void test_darkPanelReadsSlowest() {
    TEST_ASSERT_EQUAL_UINT32(kTouchReadPanelOffMs,
                             alert_touch_read_period_ms_rs(kAlertDisplayIdleOff, 0));
}

}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_quietPanelWalksLiveDimOff);
    RUN_TEST(test_criticalAlertKeepsTheDarkPanelLive);
    RUN_TEST(test_recentTouchReadsAtTheLvglRate);
    RUN_TEST(test_untouchedPanelReadsSlower);
    RUN_TEST(test_darkPanelReadsSlowest);
    return UNITY_END();
}
