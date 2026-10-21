
#include "board_config.h"
#include "config/config_loader.h"
#include "diag/error_store.h"
#include "hal/storage/storage_driver.h"

#include "config_loader_test_support.h"
#include "fixtures/dashboard_minimal.json.h"
#include "fixtures/signals_minimal.json.h"

#include <string.h>
#include <unity.h>

using ConfigLoaderTest::errorStoreHolds;
using ConfigLoaderTest::kDashboardBakPath;
using ConfigLoaderTest::outcomeIs;
using ConfigLoaderTest::stageDashboard;
using ConfigLoaderTest::stageMinimalFiles;

void setUp() {}
void tearDown() {}

void test_loadDashboard_minimalValidJson_populatesStruct() {
    stageMinimalFiles();

    const ConfigLoader::LoadResult result = ConfigLoader::loadAll();
    TEST_ASSERT_TRUE(result.dashboardOk);
    TEST_ASSERT_TRUE(result.signalsOk);

    const CfgDashboard &dashboard = ConfigLoader::getDashboardConfig();
    TEST_ASSERT_TRUE(dashboard.loaded);
    TEST_ASSERT_EQUAL_STRING("Minimal Test Dashboard", dashboard.name);
    TEST_ASSERT_EQUAL_STRING("main", dashboard.defaultPageId);
    TEST_ASSERT_EQUAL_UINT8(1, dashboard.pageCount);
    TEST_ASSERT_EQUAL_STRING("main", dashboard.pages[0].id);

    const CfgSignalConfig &signals = ConfigLoader::getSignalConfig();
    TEST_ASSERT_TRUE(signals.loaded);
    TEST_ASSERT_EQUAL_UINT8(1, signals.signalCount);
    TEST_ASSERT_EQUAL_STRING("rpm", signals.signals[0].name);
    TEST_ASSERT_EQUAL_UINT32(0x370u, signals.signals[0].canFrameId);
}

void test_loadDashboard_invalidSchemaVersion_logsAndUsesFallback() {
    StorageDriver::fakeReset();
    StorageDriver::fakeWrite(kConfigPathDashboard, fixtures::kDashboardWrongVersion,
                             strlen(fixtures::kDashboardWrongVersion));
    StorageDriver::fakeWrite(kConfigPathSignals, fixtures::kSignalsMinimal,
                             strlen(fixtures::kSignalsMinimal));

    const ConfigLoader::LoadResult result = ConfigLoader::loadAll();
    TEST_ASSERT_TRUE(result.dashboardOk);

    const CfgDashboard &dashboard = ConfigLoader::getDashboardConfig();
    TEST_ASSERT_TRUE(dashboard.loaded);
    TEST_ASSERT_EQUAL_STRING("99.0.0", dashboard.version);
    TEST_ASSERT_EQUAL_STRING("Wrong Version", dashboard.name);
    TEST_ASSERT_EQUAL_UINT8(1, dashboard.pageCount);
}

void test_reload_picks_up_new_dashboard() {
    stageMinimalFiles();
    TEST_ASSERT_TRUE(ConfigLoader::loadAll().dashboardOk);
    TEST_ASSERT_EQUAL_UINT8(1, ConfigLoader::getDashboardConfig().pageCount);
    TEST_ASSERT_EQUAL_STRING("Minimal Test Dashboard", ConfigLoader::getDashboardConfig().name);

    StorageDriver::fakeReset();
    StorageDriver::fakeWrite(kConfigPathDashboard, fixtures::kDashboardMinimalReload,
                             strlen(fixtures::kDashboardMinimalReload));
    StorageDriver::fakeWrite(kConfigPathSignals, fixtures::kSignalsMinimal,
                             strlen(fixtures::kSignalsMinimal));

    TEST_ASSERT_TRUE(ConfigLoader::reloadAll());

    const CfgDashboard &reloaded = ConfigLoader::getDashboardConfig();
    TEST_ASSERT_TRUE(reloaded.loaded);
    TEST_ASSERT_EQUAL_STRING("Reloaded Dashboard", reloaded.name);
    TEST_ASSERT_EQUAL_UINT8(2, reloaded.pageCount);
    TEST_ASSERT_EQUAL_STRING("first", reloaded.pages[0].id);
    TEST_ASSERT_EQUAL_STRING("second", reloaded.pages[1].id);
}

void test_reload_returns_false_on_invalid_dashboard() {
    StorageDriver::fakeReset();
    StorageDriver::fakeWrite(kConfigPathDashboard, fixtures::kDashboardCorrupt,
                             strlen(fixtures::kDashboardCorrupt));
    StorageDriver::fakeWrite(kConfigPathSignals, fixtures::kSignalsMinimal,
                             strlen(fixtures::kSignalsMinimal));

    TEST_ASSERT_FALSE(ConfigLoader::reloadAll());
}

void test_reloadAll_invalidJson_preservesPriorState() {
    stageMinimalFiles();
    TEST_ASSERT_TRUE(ConfigLoader::loadAll().dashboardOk);

    const CfgDashboard &before = ConfigLoader::getDashboardConfig();
    TEST_ASSERT_TRUE(before.loaded);
    TEST_ASSERT_EQUAL_STRING("Minimal Test Dashboard", before.name);
    TEST_ASSERT_EQUAL_STRING("main", before.defaultPageId);
    TEST_ASSERT_EQUAL_UINT8(1, before.pageCount);
    TEST_ASSERT_EQUAL_STRING("main", before.pages[0].id);
    const float revLimitBefore = before.revLimitRpm;
    const uint8_t pageCountBefore = before.pageCount;

    StorageDriver::fakeReset();
    StorageDriver::fakeWrite(kConfigPathDashboard, fixtures::kDashboardCorrupt,
                             strlen(fixtures::kDashboardCorrupt));
    StorageDriver::fakeWrite(kConfigPathSignals, fixtures::kSignalsMinimal,
                             strlen(fixtures::kSignalsMinimal));
    TEST_ASSERT_FALSE(ConfigLoader::reloadAll());

    const CfgDashboard &after = ConfigLoader::getDashboardConfig();
    TEST_ASSERT_TRUE(after.loaded);
    TEST_ASSERT_EQUAL_STRING("Minimal Test Dashboard", after.name);
    TEST_ASSERT_EQUAL_STRING("main", after.defaultPageId);
    TEST_ASSERT_EQUAL_UINT8(pageCountBefore, after.pageCount);
    TEST_ASSERT_EQUAL_STRING("main", after.pages[0].id);
    TEST_ASSERT_EQUAL_FLOAT(revLimitBefore, after.revLimitRpm);
}

void test_reloadAll_validJson_replacesPriorState() {
    stageMinimalFiles();
    TEST_ASSERT_TRUE(ConfigLoader::loadAll().dashboardOk);
    TEST_ASSERT_EQUAL_STRING("Minimal Test Dashboard", ConfigLoader::getDashboardConfig().name);

    StorageDriver::fakeReset();
    StorageDriver::fakeWrite(kConfigPathDashboard, fixtures::kDashboardMinimalReload,
                             strlen(fixtures::kDashboardMinimalReload));
    StorageDriver::fakeWrite(kConfigPathSignals, fixtures::kSignalsMinimal,
                             strlen(fixtures::kSignalsMinimal));
    TEST_ASSERT_TRUE(ConfigLoader::reloadAll());

    const CfgDashboard &after = ConfigLoader::getDashboardConfig();
    TEST_ASSERT_EQUAL_STRING("Reloaded Dashboard", after.name);
    TEST_ASSERT_EQUAL_UINT8(2, after.pageCount);
    TEST_ASSERT_EQUAL_STRING("first", after.pages[0].id);
    TEST_ASSERT_EQUAL_STRING("second", after.pages[1].id);
}

void test_reloadAll_invalidSignals_preservesSignalState() {
    stageMinimalFiles();
    TEST_ASSERT_TRUE(ConfigLoader::loadAll().signalsOk);

    const CfgSignalConfig &before = ConfigLoader::getSignalConfig();
    TEST_ASSERT_TRUE(before.loaded);
    TEST_ASSERT_EQUAL_UINT8(1, before.signalCount);
    TEST_ASSERT_EQUAL_STRING("rpm", before.signals[0].name);
    TEST_ASSERT_EQUAL_UINT32(0x370u, before.signals[0].canFrameId);
    const uint8_t signalCountBefore = before.signalCount;

    StorageDriver::fakeReset();
    StorageDriver::fakeWrite(kConfigPathDashboard, fixtures::kDashboardMinimal,
                             strlen(fixtures::kDashboardMinimal));
    StorageDriver::fakeWrite(kConfigPathSignals, fixtures::kSignalsCorrupt,
                             strlen(fixtures::kSignalsCorrupt));
    TEST_ASSERT_TRUE(ConfigLoader::reloadAll());

    const CfgSignalConfig &after = ConfigLoader::getSignalConfig();
    TEST_ASSERT_TRUE(after.loaded);
    TEST_ASSERT_EQUAL_UINT8(signalCountBefore, after.signalCount);
    TEST_ASSERT_EQUAL_STRING("rpm", after.signals[0].name);
    TEST_ASSERT_EQUAL_UINT32(0x370u, after.signals[0].canFrameId);
}

void test_loadDashboard_targetProfile_defaultsToCrowpanel28WhenAbsent() {
    stageMinimalFiles();
    TEST_ASSERT_TRUE(ConfigLoader::loadAll().dashboardOk);
    TEST_ASSERT_EQUAL_STRING("crowpanel-28", ConfigLoader::getDashboardConfig().targetProfile);
}

void test_loadDashboard_targetProfile_roundTripsExplicitValue() {
    StorageDriver::fakeReset();
    StorageDriver::fakeWrite(kConfigPathDashboard, fixtures::kDashboardWithProfile,
                             strlen(fixtures::kDashboardWithProfile));
    StorageDriver::fakeWrite(kConfigPathSignals, fixtures::kSignalsMinimal,
                             strlen(fixtures::kSignalsMinimal));
    TEST_ASSERT_TRUE(ConfigLoader::loadAll().dashboardOk);
    TEST_ASSERT_EQUAL_STRING("crowpanel-28", ConfigLoader::getDashboardConfig().targetProfile);
}

void test_loadDashboard_pageTemplate_parsesCruiseControlAndDefaultsCustom() {
    StorageDriver::fakeReset();
    StorageDriver::fakeWrite(kConfigPathDashboard, fixtures::kDashboardWithTemplates,
                             strlen(fixtures::kDashboardWithTemplates));
    StorageDriver::fakeWrite(kConfigPathSignals, fixtures::kSignalsMinimal,
                             strlen(fixtures::kSignalsMinimal));

    TEST_ASSERT_TRUE(ConfigLoader::loadAll().dashboardOk);

    const CfgDashboard &dashboard = ConfigLoader::getDashboardConfig();
    TEST_ASSERT_EQUAL_UINT8(2, dashboard.pageCount);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CfgPageTemplate::CUSTOM),
                            static_cast<uint8_t>(dashboard.pages[0].templateKind));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CfgPageTemplate::CRUISE_CONTROL),
                            static_cast<uint8_t>(dashboard.pages[1].templateKind));
}

void test_loadDashboard_outOfRangeLayout_clampsToGrid() {
    StorageDriver::fakeReset();
    StorageDriver::fakeWrite(kConfigPathDashboard, fixtures::kDashboardLayoutOutOfRange,
                             strlen(fixtures::kDashboardLayoutOutOfRange));
    StorageDriver::fakeWrite(kConfigPathSignals, fixtures::kSignalsMinimal,
                             strlen(fixtures::kSignalsMinimal));

    TEST_ASSERT_TRUE(ConfigLoader::loadAll().dashboardOk);

    const CfgPage &page = ConfigLoader::getDashboardConfig().pages[0];
    TEST_ASSERT_EQUAL_UINT8(4, page.widgetCount);

    TEST_ASSERT_EQUAL_UINT8(0, page.widgets[0].layout.col);
    TEST_ASSERT_EQUAL_UINT8(3, page.widgets[0].layout.colSpan);
    TEST_ASSERT_EQUAL_UINT8(0, page.widgets[0].layout.row);
    TEST_ASSERT_EQUAL_UINT8(2, page.widgets[0].layout.rowSpan);

    TEST_ASSERT_EQUAL_UINT8(9, page.widgets[1].layout.col);
    TEST_ASSERT_EQUAL_UINT8(3, page.widgets[1].layout.colSpan);
    TEST_ASSERT_EQUAL_UINT8(10, page.widgets[1].layout.row);
    TEST_ASSERT_EQUAL_UINT8(2, page.widgets[1].layout.rowSpan);

    TEST_ASSERT_EQUAL_UINT8(4, page.widgets[2].layout.col);
    TEST_ASSERT_EQUAL_UINT8(1, page.widgets[2].layout.colSpan);
    TEST_ASSERT_EQUAL_UINT8(4, page.widgets[2].layout.row);
    TEST_ASSERT_EQUAL_UINT8(1, page.widgets[2].layout.rowSpan);

    TEST_ASSERT_EQUAL_UINT8(2, page.widgets[3].layout.col);
    TEST_ASSERT_EQUAL_UINT8(10, page.widgets[3].layout.colSpan);
    TEST_ASSERT_EQUAL_UINT8(8, page.widgets[3].layout.row);
    TEST_ASSERT_EQUAL_UINT8(4, page.widgets[3].layout.rowSpan);
}

void test_loadDashboard_themePalette_parsesTextColorsAndDayNightSignal() {
    StorageDriver::fakeReset();
    StorageDriver::fakeWrite(kConfigPathDashboard, fixtures::kDashboardWithThemes,
                             strlen(fixtures::kDashboardWithThemes));
    StorageDriver::fakeWrite(kConfigPathSignals, fixtures::kSignalsMinimal,
                             strlen(fixtures::kSignalsMinimal));

    TEST_ASSERT_TRUE(ConfigLoader::loadAll().dashboardOk);

    const CfgDashboard &dashboard = ConfigLoader::getDashboardConfig();
    TEST_ASSERT_TRUE(dashboard.hasTheme);
    TEST_ASSERT_TRUE(dashboard.dayFace.hasPalette);
    TEST_ASSERT_EQUAL_UINT32(0xE8E6E1u, dashboard.dayFace.bgColor.rgb);
    TEST_ASSERT_EQUAL_UINT32(0x141414u, dashboard.dayFace.text.rgb);
    TEST_ASSERT_EQUAL_UINT32(0x6A665Fu, dashboard.dayFace.textDim.rgb);

    TEST_ASSERT_FALSE(dashboard.nightFace.hasPalette);
    TEST_ASSERT_EQUAL_UINT32(0x121212u, dashboard.nightFace.bgColor.rgb);

    TEST_ASSERT_EQUAL_STRING("flag_headlights", dashboard.dayNightSignal);
}

void test_loadDashboard_noThemes_leavesDayNightSignalEmpty() {
    stageMinimalFiles();

    TEST_ASSERT_TRUE(ConfigLoader::loadAll().dashboardOk);

    const CfgDashboard &dashboard = ConfigLoader::getDashboardConfig();
    TEST_ASSERT_FALSE(dashboard.hasTheme);
    TEST_ASSERT_EQUAL_STRING("", dashboard.dayNightSignal);
}

void test_loadDashboard_partialPaletteAndOverlongSignal_fallBackSafely() {
    StorageDriver::fakeReset();
    StorageDriver::fakeWrite(kConfigPathDashboard, fixtures::kDashboardThemeEdgeCases,
                             strlen(fixtures::kDashboardThemeEdgeCases));
    StorageDriver::fakeWrite(kConfigPathSignals, fixtures::kSignalsMinimal,
                             strlen(fixtures::kSignalsMinimal));

    TEST_ASSERT_TRUE(ConfigLoader::loadAll().dashboardOk);

    const CfgDashboard &dashboard = ConfigLoader::getDashboardConfig();
    TEST_ASSERT_TRUE(dashboard.hasTheme);
    TEST_ASSERT_FALSE(dashboard.dayFace.hasPalette);
    TEST_ASSERT_FALSE(dashboard.nightFace.hasPalette);
    TEST_ASSERT_EQUAL_STRING("", dashboard.dayNightSignal);
}

void test_loadDashboard_gearBig_isParsed() {
    stageDashboard(fixtures::kDashboardGearBig, nullptr);

    TEST_ASSERT_TRUE(ConfigLoader::loadAll().dashboardOk);

    const CfgWidget &gear = ConfigLoader::getDashboardConfig().pages[0].widgets[0];
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WidgetType::GEAR_IND),
                            static_cast<uint8_t>(gear.type));
    TEST_ASSERT_EQUAL_UINT8(96, gear.label.big);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_loadDashboard_minimalValidJson_populatesStruct);
    RUN_TEST(test_loadDashboard_invalidSchemaVersion_logsAndUsesFallback);
    RUN_TEST(test_reload_picks_up_new_dashboard);
    RUN_TEST(test_reload_returns_false_on_invalid_dashboard);
    RUN_TEST(test_reloadAll_invalidJson_preservesPriorState);
    RUN_TEST(test_reloadAll_validJson_replacesPriorState);
    RUN_TEST(test_reloadAll_invalidSignals_preservesSignalState);
    RUN_TEST(test_loadDashboard_targetProfile_defaultsToCrowpanel28WhenAbsent);
    RUN_TEST(test_loadDashboard_targetProfile_roundTripsExplicitValue);
    RUN_TEST(test_loadDashboard_pageTemplate_parsesCruiseControlAndDefaultsCustom);
    RUN_TEST(test_loadDashboard_outOfRangeLayout_clampsToGrid);
    RUN_TEST(test_loadDashboard_themePalette_parsesTextColorsAndDayNightSignal);
    RUN_TEST(test_loadDashboard_noThemes_leavesDayNightSignalEmpty);
    RUN_TEST(test_loadDashboard_partialPaletteAndOverlongSignal_fallBackSafely);
    RUN_TEST(test_loadSignals_malformedCanFrameId_dropsSignal);
    RUN_TEST(test_loadSignals_threeByteValue_isAccepted);
    RUN_TEST(test_loadSignals_nameTooLong_dropsSignal);
    RUN_TEST(test_loadSignals_unitTooLong_resetsUnitToEmpty);
    RUN_TEST(test_loadDashboard_gearBig_isParsed);
    runDashboardOutcomeTests();
    return UNITY_END();
}
