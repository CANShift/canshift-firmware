#include "alert_engine_rs.h"

#include <stdint.h>
#include <unity.h>

namespace {

constexpr int8_t kOilPressure = 0;
constexpr int8_t kCoolant = 1;
constexpr int8_t kUserCritical = 7;

uint32_t s_acknowledged = 0;

uint32_t bit(int8_t source) {
    return 1u << source;
}

int8_t takeoverFor(uint32_t critical) {
    s_acknowledged = alert_critical_retain_rs(s_acknowledged, critical);
    return alert_critical_first_unacknowledged_rs(critical, s_acknowledged);
}

void acknowledge(int8_t source) {
    s_acknowledged = alert_critical_acknowledge_rs(s_acknowledged, source);
}

}

void setUp() {
    s_acknowledged = 0;
}

void tearDown() {}

void test_no_critical_source_shows_no_takeover() {
    TEST_ASSERT_EQUAL_INT8(kAlertCriticalNoSource, takeoverFor(0));
}

void test_acknowledged_oil_pressure_does_not_mask_coolant() {
    TEST_ASSERT_EQUAL_INT8(kOilPressure, takeoverFor(bit(kOilPressure)));
    acknowledge(kOilPressure);
    TEST_ASSERT_EQUAL_INT8(kAlertCriticalNoSource, takeoverFor(bit(kOilPressure)));
    TEST_ASSERT_EQUAL_INT8(kCoolant, takeoverFor(bit(kOilPressure) | bit(kCoolant)));
}

void test_recovered_oil_pressure_shows_again_when_it_re_trips() {
    TEST_ASSERT_EQUAL_INT8(kOilPressure, takeoverFor(bit(kOilPressure)));
    acknowledge(kOilPressure);
    TEST_ASSERT_EQUAL_INT8(kAlertCriticalNoSource, takeoverFor(0));
    TEST_ASSERT_EQUAL_INT8(kOilPressure, takeoverFor(bit(kOilPressure)));
}

void test_user_critical_is_not_masked_by_a_built_in_acknowledge() {
    TEST_ASSERT_EQUAL_INT8(kCoolant, takeoverFor(bit(kCoolant)));
    acknowledge(kCoolant);
    TEST_ASSERT_EQUAL_INT8(kUserCritical, takeoverFor(bit(kCoolant) | bit(kUserCritical)));
}

void test_acknowledge_without_a_takeover_is_ignored() {
    acknowledge(kAlertCriticalNoSource);
    TEST_ASSERT_EQUAL_INT8(kOilPressure, takeoverFor(bit(kOilPressure)));
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_no_critical_source_shows_no_takeover);
    RUN_TEST(test_acknowledged_oil_pressure_does_not_mask_coolant);
    RUN_TEST(test_recovered_oil_pressure_shows_again_when_it_re_trips);
    RUN_TEST(test_user_critical_is_not_masked_by_a_built_in_acknowledge);
    RUN_TEST(test_acknowledge_without_a_takeover_is_ignored);
    return UNITY_END();
}
