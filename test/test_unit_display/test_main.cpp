#include "board_config.h"
#include "config/config_loader.h"
#include "hal/storage/storage_driver.h"
#include "ui/unit_display.h"

#include "fixtures/dashboard_units.json.h"

#include <math.h>
#include <string.h>
#include <unity.h>

namespace {

constexpr float kEpsilon = 0.01f;

void stageDashboard(const char *json) {
    StorageDriver::fakeReset();
    StorageDriver::fakeWrite(CONFIG_PATH_DASHBOARD, json, strlen(json));
    const ConfigLoader::LoadResult result = ConfigLoader::loadAll();
    TEST_ASSERT_TRUE(result.dashboardOk);
}

} // namespace

void setUp() {}
void tearDown() {}

void test_units_absent_readsAsMetric() {
    stageDashboard(fixtures::kDashboardNoUnits);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CfgUnitSystem::METRIC),
                            static_cast<uint8_t>(ConfigLoader::getDashboardConfig().units));
}

void test_units_unknownValue_fallsBackToMetric() {
    stageDashboard(fixtures::kDashboardBogusUnits);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CfgUnitSystem::METRIC),
                            static_cast<uint8_t>(ConfigLoader::getDashboardConfig().units));
}

void test_units_imperial_parsesFromDashboardJson() {
    stageDashboard(fixtures::kDashboardImperial);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CfgUnitSystem::IMPERIAL),
                            static_cast<uint8_t>(ConfigLoader::getDashboardConfig().units));
}

void test_metricConfig_leavesEveryPairedUnitAlone() {
    stageDashboard(fixtures::kDashboardMetric);
    TEST_ASSERT_EQUAL_STRING("km/h", UnitDisplay::symbolFor("km/h"));
    TEST_ASSERT_EQUAL_STRING("°C", UnitDisplay::symbolFor("°C"));
    TEST_ASSERT_EQUAL_STRING("bar", UnitDisplay::symbolFor("bar"));
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, 100.0f, UnitDisplay::valueFor(100.0f, "km/h"));
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, 90.0f, UnitDisplay::valueFor(90.0f, "°C"));
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, 2.5f, UnitDisplay::valueFor(2.5f, "bar"));
}

void test_imperialConfig_swapsSymbolAndValue() {
    stageDashboard(fixtures::kDashboardImperial);
    TEST_ASSERT_EQUAL_STRING("mph", UnitDisplay::symbolFor("km/h"));
    TEST_ASSERT_EQUAL_STRING("°F", UnitDisplay::symbolFor("°C"));
    TEST_ASSERT_EQUAL_STRING("psi", UnitDisplay::symbolFor("bar"));
    TEST_ASSERT_EQUAL_STRING("mi", UnitDisplay::symbolFor("km"));
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, 62.14f, UnitDisplay::valueFor(100.0f, "km/h"));
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, 194.0f, UnitDisplay::valueFor(90.0f, "°C"));
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, 36.26f, UnitDisplay::valueFor(2.5f, "bar"));
}

void test_imperialConfig_leavesUnpairedUnitsAlone() {
    stageDashboard(fixtures::kDashboardImperial);
    TEST_ASSERT_EQUAL_STRING("%", UnitDisplay::symbolFor("%"));
    TEST_ASSERT_EQUAL_STRING("V", UnitDisplay::symbolFor("V"));
    TEST_ASSERT_EQUAL_STRING("rpm", UnitDisplay::symbolFor("rpm"));
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, 13.8f, UnitDisplay::valueFor(13.8f, "V"));
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, 6500.0f, UnitDisplay::valueFor(6500.0f, "rpm"));
}

void test_emptyAndNullUnits_passThroughUnchanged() {
    stageDashboard(fixtures::kDashboardImperial);
    TEST_ASSERT_EQUAL_STRING("", UnitDisplay::symbolFor(""));
    TEST_ASSERT_NULL(UnitDisplay::symbolFor(nullptr));
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, 7.5f, UnitDisplay::valueFor(7.5f, ""));
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, 7.5f, UnitDisplay::valueFor(7.5f, nullptr));
}

void test_authoredSuffix_isVerbatimAndNeverConverts() {
    stageDashboard(fixtures::kDashboardImperial);
    TEST_ASSERT_EQUAL_STRING("km/h", UnitDisplay::displayUnitFor("speed_kph", "km/h"));
    TEST_ASSERT_EQUAL_STRING("", UnitDisplay::convertibleUnitFor("speed_kph", "km/h"));
    const char *convertible = UnitDisplay::convertibleUnitFor("speed_kph", "km/h");
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, 100.0f, UnitDisplay::valueFor(100.0f, convertible));
}

void test_unauthoredSuffix_convertsFromTheSignalUnit() {
    stageDashboard(fixtures::kDashboardImperial);
    TEST_ASSERT_EQUAL_STRING("mph", UnitDisplay::displayUnitFor("speed_kph", ""));
    const char *convertible = UnitDisplay::convertibleUnitFor("speed_kph", "");
    TEST_ASSERT_EQUAL_STRING("km/h", convertible);
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, 62.14f, UnitDisplay::valueFor(100.0f, convertible));
}

void test_reloadingIntoMetric_restoresMetricRendering() {
    stageDashboard(fixtures::kDashboardImperial);
    TEST_ASSERT_EQUAL_STRING("mph", UnitDisplay::symbolFor("km/h"));
    stageDashboard(fixtures::kDashboardMetric);
    TEST_ASSERT_EQUAL_STRING("km/h", UnitDisplay::symbolFor("km/h"));
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, 100.0f, UnitDisplay::valueFor(100.0f, "km/h"));
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_units_absent_readsAsMetric);
    RUN_TEST(test_units_unknownValue_fallsBackToMetric);
    RUN_TEST(test_units_imperial_parsesFromDashboardJson);
    RUN_TEST(test_metricConfig_leavesEveryPairedUnitAlone);
    RUN_TEST(test_imperialConfig_swapsSymbolAndValue);
    RUN_TEST(test_imperialConfig_leavesUnpairedUnitsAlone);
    RUN_TEST(test_emptyAndNullUnits_passThroughUnchanged);
    RUN_TEST(test_authoredSuffix_isVerbatimAndNeverConverts);
    RUN_TEST(test_unauthoredSuffix_convertsFromTheSignalUnit);
    RUN_TEST(test_reloadingIntoMetric_restoresMetricRendering);
    return UNITY_END();
}
