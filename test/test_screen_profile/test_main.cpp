#include "board_config.h"
#include "config/config_loader.h"
#include "hal/storage/storage_driver.h"
#include "hardware_profile.h"
#include "ui/screen_profile.h"

#include <string.h>
#include <unity.h>

namespace {

constexpr const char *kDashboardCrowpanel28 = R"({
  "version": "1.0.0",
  "name": "Profile Smoke",
  "defaultPageId": "main",
  "revLimitRpm": 7000,
  "targetProfile": "crowpanel-28",
  "topBar": {"height": 24, "bgColor": "#111111", "textColor": "#FFFFFF"},
  "pages": [
    {"id": "main", "backgroundColor": "#111111", "showTopBar": true, "widgets": []}
  ]
})";

constexpr const char *kSignalsMinimal = R"({
  "version": "1.0.0",
  "protocol": "test",
  "canSpeedKbps": 500,
  "signals": []
})";

void stageDashboard(const char *dashboard) {
    StorageDriver::fakeReset();
    StorageDriver::fakeWrite(kConfigPathDashboard, dashboard, strlen(dashboard));
    StorageDriver::fakeWrite(kConfigPathSignals, kSignalsMinimal, strlen(kSignalsMinimal));
    TEST_ASSERT_TRUE(ConfigLoader::loadAll().dashboardOk);
}

}

void setUp() {}
void tearDown() {}

void test_lookupDesignDimensions_knownProfile() {
    const ScreenProfile::DesignDimensions dims =
        ScreenProfile::lookupDesignDimensions("crowpanel-28");
    TEST_ASSERT_EQUAL_UINT16(320, dims.width);
    TEST_ASSERT_EQUAL_UINT16(240, dims.height);
}

void test_lookupDesignDimensions_unknownFallsBackToDefault() {
    const ScreenProfile::DesignDimensions dims =
        ScreenProfile::lookupDesignDimensions("crowpanel-9999");
    TEST_ASSERT_EQUAL_UINT16(320, dims.width);
    TEST_ASSERT_EQUAL_UINT16(240, dims.height);
}

void test_lookupDesignDimensions_emptyStringFallsBackToDefault() {
    const ScreenProfile::DesignDimensions empty = ScreenProfile::lookupDesignDimensions("");
    TEST_ASSERT_EQUAL_UINT16(320, empty.width);
    TEST_ASSERT_EQUAL_UINT16(240, empty.height);

    const ScreenProfile::DesignDimensions null = ScreenProfile::lookupDesignDimensions(nullptr);
    TEST_ASSERT_EQUAL_UINT16(320, null.width);
    TEST_ASSERT_EQUAL_UINT16(240, null.height);
}

void test_initFromDashboard_crowpanel28_yieldsIdentityFactors() {
    stageDashboard(kDashboardCrowpanel28);
    ScreenProfile::initFromDashboard();

    const ScreenProfile::ScaleFactors factors = ScreenProfile::getScaleFactors();
    TEST_ASSERT_EQUAL_FLOAT(1.0f, factors.x);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, factors.y);
}

void test_scaleXVal_scaleYVal_identityRoundtripsCoordinates() {
    stageDashboard(kDashboardCrowpanel28);
    ScreenProfile::initFromDashboard();

    TEST_ASSERT_EQUAL_INT16(0, ScreenProfile::scaleXVal(0));
    TEST_ASSERT_EQUAL_INT16(0, ScreenProfile::scaleYVal(0));
    TEST_ASSERT_EQUAL_INT16(160, ScreenProfile::scaleXVal(160));
    TEST_ASSERT_EQUAL_INT16(112, ScreenProfile::scaleYVal(112));
    TEST_ASSERT_EQUAL_INT16(320, ScreenProfile::scaleXVal(320));
    TEST_ASSERT_EQUAL_INT16(240, ScreenProfile::scaleYVal(240));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_lookupDesignDimensions_knownProfile);
    RUN_TEST(test_lookupDesignDimensions_unknownFallsBackToDefault);
    RUN_TEST(test_lookupDesignDimensions_emptyStringFallsBackToDefault);
    RUN_TEST(test_initFromDashboard_crowpanel28_yieldsIdentityFactors);
    RUN_TEST(test_scaleXVal_scaleYVal_identityRoundtripsCoordinates);
    return UNITY_END();
}
