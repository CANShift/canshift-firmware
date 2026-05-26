// test_main.cpp — Unity tests for ScreenProfile (issues #17, #18).
//
// Targets the v1 scaffold: lookup is identity for the only known profile,
// unknowns degrade to the default, and the public scale helpers emit values
// equal to their inputs when design dims == physical dims.

#include "board_config.h"
#include "config/config_loader.h"
#include "hal/storage/storage_driver.h"
#include "hardware_profile.h"
#include "ui/screen_profile.h"

#include <string.h>
#include <unity.h>

namespace {

// Minimal dashboard with an explicit targetProfile so initFromDashboard()
// reads a stable id rather than the default fallback.
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
    StorageDriver::fakeWrite(CONFIG_PATH_DASHBOARD, dashboard, strlen(dashboard));
    StorageDriver::fakeWrite(CONFIG_PATH_SIGNALS, kSignalsMinimal, strlen(kSignalsMinimal));
    TEST_ASSERT_TRUE(ConfigLoader::loadAll().dashboardOk);
}

} // namespace

void setUp() {}
void tearDown() {}

// Catalog hit: crowpanel-28 must resolve to the v1 design canvas (320×240).
void test_lookupDesignDimensions_knownProfile() {
    const ScreenProfile::DesignDimensions dims =
        ScreenProfile::lookupDesignDimensions("crowpanel-28");
    TEST_ASSERT_EQUAL_UINT16(320, dims.width);
    TEST_ASSERT_EQUAL_UINT16(240, dims.height);
}

// Catalog miss: an unknown / nonexistent id must fall back to the default
// profile rather than zeroing dims (would divide by zero downstream).
void test_lookupDesignDimensions_unknownFallsBackToDefault() {
    const ScreenProfile::DesignDimensions dims =
        ScreenProfile::lookupDesignDimensions("crowpanel-9999");
    TEST_ASSERT_EQUAL_UINT16(320, dims.width);
    TEST_ASSERT_EQUAL_UINT16(240, dims.height);
}

// Empty / null id must also fall back to the default.
void test_lookupDesignDimensions_emptyStringFallsBackToDefault() {
    const ScreenProfile::DesignDimensions empty = ScreenProfile::lookupDesignDimensions("");
    TEST_ASSERT_EQUAL_UINT16(320, empty.width);
    TEST_ASSERT_EQUAL_UINT16(240, empty.height);

    const ScreenProfile::DesignDimensions null = ScreenProfile::lookupDesignDimensions(nullptr);
    TEST_ASSERT_EQUAL_UINT16(320, null.width);
    TEST_ASSERT_EQUAL_UINT16(240, null.height);
}

// v1 invariant — design dims == physical dims → identity factors. This is the
// scale-of-1.0 guarantee the PR rests on: widget render output is byte-for-byte
// identical to pre-scaffold firmware on the only board that ships today.
void test_initFromDashboard_crowpanel28_yieldsIdentityFactors() {
    stageDashboard(kDashboardCrowpanel28);
    ScreenProfile::initFromDashboard();

    const ScreenProfile::ScaleFactors factors = ScreenProfile::getScaleFactors();
    TEST_ASSERT_EQUAL_FLOAT(1.0f, factors.x);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, factors.y);
}

// Identity scale must round-trip representative widget coordinates (the four
// extremes a v1 dashboard uses: top-left, top-right, full-width, full-height).
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

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_lookupDesignDimensions_knownProfile);
    RUN_TEST(test_lookupDesignDimensions_unknownFallsBackToDefault);
    RUN_TEST(test_lookupDesignDimensions_emptyStringFallsBackToDefault);
    RUN_TEST(test_initFromDashboard_crowpanel28_yieldsIdentityFactors);
    RUN_TEST(test_scaleXVal_scaleYVal_identityRoundtripsCoordinates);
    return UNITY_END();
}
