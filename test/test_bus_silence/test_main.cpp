#include "alert_engine_rs.h"

#include <unity.h>

namespace {

constexpr uint32_t kThresholdMs = 500;
constexpr uint32_t kNeverMs = UINT32_MAX;

AlertBusSilenceRs sample(uint32_t msSinceRx, uint32_t uptimeMs) {
    AlertBusSilenceRs out = {};
    alert_bus_silence_rs(&out, msSinceRx, uptimeMs, kThresholdMs);
    return out;
}

void test_liveBusIsNotSilent() {
    const AlertBusSilenceRs s = sample(499, 60000);
    TEST_ASSERT_FALSE(s.silent);
    TEST_ASSERT_EQUAL_UINT32(0, s.seconds);
}

void test_thresholdIsInclusive() {
    TEST_ASSERT_TRUE(sample(kThresholdMs, 60000).silent);
}

void test_secondsFloorToWholeSeconds() {
    TEST_ASSERT_EQUAL_UINT32(4, sample(4999, 60000).seconds);
    TEST_ASSERT_EQUAL_UINT32(5, sample(5000, 60000).seconds);
}

void test_neverReceivedCountsFromUptime() {
    const AlertBusSilenceRs s = sample(kNeverMs, 4200);
    TEST_ASSERT_TRUE(s.silent);
    TEST_ASSERT_EQUAL_UINT32(4, s.seconds);
}

void test_neverReceivedBeforeThresholdIsNotSilent() {
    TEST_ASSERT_FALSE(sample(kNeverMs, 120).silent);
}

void test_onlyFourDigitValuesWidenThePlaceholder() {
    TEST_ASSERT_EQUAL_UINT8(2, alert_stale_dash_groups_rs(16.0f));
    TEST_ASSERT_EQUAL_UINT8(2, alert_stale_dash_groups_rs(300.0f));
    TEST_ASSERT_EQUAL_UINT8(ALERT_STALE_DASH_GROUPS_MAX, alert_stale_dash_groups_rs(7200.0f));
    TEST_ASSERT_EQUAL_UINT8(ALERT_STALE_DASH_GROUPS_MAX, alert_stale_dash_groups_rs(12000.0f));
}

} // namespace

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_liveBusIsNotSilent);
    RUN_TEST(test_thresholdIsInclusive);
    RUN_TEST(test_secondsFloorToWholeSeconds);
    RUN_TEST(test_neverReceivedCountsFromUptime);
    RUN_TEST(test_neverReceivedBeforeThresholdIsNotSilent);
    RUN_TEST(test_onlyFourDigitValuesWidenThePlaceholder);
    return UNITY_END();
}
