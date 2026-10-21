#include "runtime/ota_soak.h"

#include <unity.h>

namespace {

using OtaSoak::Moment;

constexpr uint32_t kLongUptimeMs = OtaSoak::kSoakMs * 10;

}

void setUp() {}

void tearDown() {}

void test_theFirstDashFramesDoNotValidate() {
    TEST_ASSERT_FALSE(OtaSoak::marksValid(Moment::DashFrame, 300));
    TEST_ASSERT_FALSE(OtaSoak::marksValid(Moment::DashFrame, OtaSoak::kSoakMs - 1));
}

void test_aDashFrameAfterTheSoakValidates() {
    TEST_ASSERT_TRUE(OtaSoak::marksValid(Moment::DashFrame, OtaSoak::kSoakMs));
}

void test_workshopFramesNeverValidate() {
    TEST_ASSERT_FALSE(OtaSoak::marksValid(Moment::WorkshopFrame, kLongUptimeMs));
}

void test_leavingWorkshopValidatesOnlyAfterTheSoak() {
    TEST_ASSERT_FALSE(OtaSoak::marksValid(Moment::LeavingWorkshop, OtaSoak::kSoakMs - 1));
    TEST_ASSERT_TRUE(OtaSoak::marksValid(Moment::LeavingWorkshop, kLongUptimeMs));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_theFirstDashFramesDoNotValidate);
    RUN_TEST(test_aDashFrameAfterTheSoakValidates);
    RUN_TEST(test_workshopFramesNeverValidate);
    RUN_TEST(test_leavingWorkshopValidatesOnlyAfterTheSoak);
    return UNITY_END();
}
