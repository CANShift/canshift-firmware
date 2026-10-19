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

void test_loadDashboard_belowGridFloor_reportsVersionTooOld() {
    stageDashboard(fixtures::kDashboardPreGrid, nullptr);

    TEST_ASSERT_TRUE(ConfigLoader::loadAll().dashboardOk);
    TEST_ASSERT_TRUE(errorStoreHolds("VER_OLD"));
}

void test_loadDashboard_atGridFloor_reportsNothing() {
    stageDashboard(fixtures::kDashboardGridFloor, nullptr);

    TEST_ASSERT_TRUE(ConfigLoader::loadAll().dashboardOk);
    TEST_ASSERT_FALSE(errorStoreHolds("VER_OLD"));
}

void test_loadDashboard_refusedWithBackup_restoresPrevious() {
    stageDashboard(fixtures::kDashboardOverflow, fixtures::kDashboardMinimal);

    TEST_ASSERT_TRUE(ConfigLoader::loadAll().dashboardOk);

    TEST_ASSERT_TRUE(outcomeIs(ConfigLoader::DashboardOutcome::RestoredPrevious,
                               ConfigLoader::lastDashboardOutcome()));
    TEST_ASSERT_EQUAL_STRING("Minimal Test Dashboard", ConfigLoader::getDashboardConfig().name);
    TEST_ASSERT_EQUAL_UINT32(strlen(fixtures::kDashboardMinimal),
                             StorageDriver::fileSize(kConfigPathDashboard));
    TEST_ASSERT_FALSE(StorageDriver::fileExists(kDashboardBakPath));
    TEST_ASSERT_FALSE(ConfigLoader::getRejection().present);
    TEST_ASSERT_TRUE(errorStoreHolds("OVERFLOW"));
    TEST_ASSERT_TRUE(errorStoreHolds("CFG_BAK"));
}

void test_loadDashboard_refusedWithoutBackup_isRefused() {
    stageDashboard(fixtures::kDashboardOverflow, nullptr);

    TEST_ASSERT_FALSE(ConfigLoader::loadAll().dashboardOk);

    TEST_ASSERT_TRUE(
        outcomeIs(ConfigLoader::DashboardOutcome::Refused, ConfigLoader::lastDashboardOutcome()));
    TEST_ASSERT_TRUE(ConfigLoader::getRejection().present);
    TEST_ASSERT_FALSE(errorStoreHolds("CFG_BAK"));
}

void test_vetDashboard_refused_restoresFileAndKeepsLiveConfig() {
    stageMinimalFiles();
    TEST_ASSERT_TRUE(ConfigLoader::loadAll().dashboardOk);
    stageDashboard(fixtures::kDashboardOverflow, fixtures::kDashboardMinimalReload);

    TEST_ASSERT_TRUE(
        outcomeIs(ConfigLoader::DashboardOutcome::RestoredPrevious, ConfigLoader::vetDashboard()));

    TEST_ASSERT_EQUAL_STRING("Minimal Test Dashboard", ConfigLoader::getDashboardConfig().name);
    TEST_ASSERT_EQUAL_UINT32(strlen(fixtures::kDashboardMinimalReload),
                             StorageDriver::fileSize(kConfigPathDashboard));
}

void test_vetDashboard_valid_isLoadedAndKeepsLiveConfig() {
    stageMinimalFiles();
    TEST_ASSERT_TRUE(ConfigLoader::loadAll().dashboardOk);
    stageDashboard(fixtures::kDashboardMinimalReload, fixtures::kDashboardMinimal);

    TEST_ASSERT_TRUE(
        outcomeIs(ConfigLoader::DashboardOutcome::Loaded, ConfigLoader::vetDashboard()));

    TEST_ASSERT_EQUAL_STRING("Minimal Test Dashboard", ConfigLoader::getDashboardConfig().name);
    TEST_ASSERT_TRUE(StorageDriver::fileExists(kDashboardBakPath));
}

void test_loadDashboard_unparsable_isUnreadable() {
    stageDashboard(fixtures::kDashboardCorrupt, nullptr);

    TEST_ASSERT_FALSE(ConfigLoader::loadAll().dashboardOk);

    TEST_ASSERT_TRUE(outcomeIs(ConfigLoader::DashboardOutcome::Unreadable,
                               ConfigLoader::lastDashboardOutcome()));
}
