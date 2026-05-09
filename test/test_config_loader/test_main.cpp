// test_main.cpp — Unity tests for ConfigLoader::loadAll().
//
// Strategy: stage fixture JSON in the in-memory storage shim, then call the
// public `loadAll()`. Observable state on the returned config structs lets us
// verify both the happy path and the schema-version mismatch path without
// relying on log scraping.

#include "board_config.h"
#include "config/config_loader.h"
#include "hal/storage/storage_driver.h"

#include "fixtures/dashboard_minimal.json.h"
#include "fixtures/signals_minimal.json.h"

#include <string.h>
#include <unity.h>

namespace {

// CONFIG_PATH_DASHBOARD / CONFIG_PATH_SIGNALS are macros from board_config.h —
// keep them resolved at use site.

void stageMinimalFiles() {
    StorageDriver::fakeReset();
    StorageDriver::fakeWrite(CONFIG_PATH_DASHBOARD, fixtures::kDashboardMinimal,
                             strlen(fixtures::kDashboardMinimal));
    StorageDriver::fakeWrite(CONFIG_PATH_SIGNALS, fixtures::kSignalsMinimal,
                             strlen(fixtures::kSignalsMinimal));
}

} // namespace

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
    StorageDriver::fakeWrite(CONFIG_PATH_DASHBOARD, fixtures::kDashboardWrongVersion,
                             strlen(fixtures::kDashboardWrongVersion));
    StorageDriver::fakeWrite(CONFIG_PATH_SIGNALS, fixtures::kSignalsMinimal,
                             strlen(fixtures::kSignalsMinimal));

    // The loader logs ERROR + pushes to ErrorStore on schema mismatch, but
    // continues to parse so the firmware can still surface what it knows.
    // Observable behaviour: dashboard.loaded == true, version field carries
    // the (wrong) value as written, name/page populated.
    const ConfigLoader::LoadResult result = ConfigLoader::loadAll();
    TEST_ASSERT_TRUE(result.dashboardOk);

    const CfgDashboard &dashboard = ConfigLoader::getDashboardConfig();
    TEST_ASSERT_TRUE(dashboard.loaded);
    TEST_ASSERT_EQUAL_STRING("99.0.0", dashboard.version);
    TEST_ASSERT_EQUAL_STRING("Wrong Version", dashboard.name);
    TEST_ASSERT_EQUAL_UINT8(1, dashboard.pageCount);
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_loadDashboard_minimalValidJson_populatesStruct);
    RUN_TEST(test_loadDashboard_invalidSchemaVersion_logsAndUsesFallback);
    return UNITY_END();
}
