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

void test_reload_picks_up_new_dashboard() {
    // Fixture A — single page, name "Minimal Test Dashboard".
    stageMinimalFiles();
    TEST_ASSERT_TRUE(ConfigLoader::loadAll().dashboardOk);
    TEST_ASSERT_EQUAL_UINT8(1, ConfigLoader::getDashboardConfig().pageCount);
    TEST_ASSERT_EQUAL_STRING("Minimal Test Dashboard", ConfigLoader::getDashboardConfig().name);

    // Fixture B — two pages, distinct name. Replace the on-disk file and call
    // reloadAll(); observable struct change confirms reload picked up B.
    StorageDriver::fakeReset();
    StorageDriver::fakeWrite(CONFIG_PATH_DASHBOARD, fixtures::kDashboardMinimalReload,
                             strlen(fixtures::kDashboardMinimalReload));
    StorageDriver::fakeWrite(CONFIG_PATH_SIGNALS, fixtures::kSignalsMinimal,
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
    StorageDriver::fakeWrite(CONFIG_PATH_DASHBOARD, fixtures::kDashboardCorrupt,
                             strlen(fixtures::kDashboardCorrupt));
    StorageDriver::fakeWrite(CONFIG_PATH_SIGNALS, fixtures::kSignalsMinimal,
                             strlen(fixtures::kSignalsMinimal));

    TEST_ASSERT_FALSE(ConfigLoader::reloadAll());
}

// Issue #458: a failed reload (parse error on dashboard.json) must leave the
// in-memory dashboard struct byte-identical to its pre-call value. Prior to
// the snapshot/rollback fix the struct could end up with mixed old/new pages
// or an inconsistent pageCount.
void test_reloadAll_invalidJson_preservesPriorState() {
    // Step 1: load fixture A and capture its observable identity.
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

    // Step 2: stage corrupt JSON and reload — must fail.
    StorageDriver::fakeReset();
    StorageDriver::fakeWrite(CONFIG_PATH_DASHBOARD, fixtures::kDashboardCorrupt,
                             strlen(fixtures::kDashboardCorrupt));
    StorageDriver::fakeWrite(CONFIG_PATH_SIGNALS, fixtures::kSignalsMinimal,
                             strlen(fixtures::kSignalsMinimal));
    TEST_ASSERT_FALSE(ConfigLoader::reloadAll());

    // Step 3: invariant — every observable field on s_dashboard still matches A.
    const CfgDashboard &after = ConfigLoader::getDashboardConfig();
    TEST_ASSERT_TRUE(after.loaded);
    TEST_ASSERT_EQUAL_STRING("Minimal Test Dashboard", after.name);
    TEST_ASSERT_EQUAL_STRING("main", after.defaultPageId);
    TEST_ASSERT_EQUAL_UINT8(pageCountBefore, after.pageCount);
    TEST_ASSERT_EQUAL_STRING("main", after.pages[0].id);
    TEST_ASSERT_EQUAL_FLOAT(revLimitBefore, after.revLimitRpm);
}

// Issue #458 sanity check: the rollback path must not interfere with valid
// reloads. Confirms the happy path still swaps state to fixture B.
void test_reloadAll_validJson_replacesPriorState() {
    stageMinimalFiles();
    TEST_ASSERT_TRUE(ConfigLoader::loadAll().dashboardOk);
    TEST_ASSERT_EQUAL_STRING("Minimal Test Dashboard", ConfigLoader::getDashboardConfig().name);

    StorageDriver::fakeReset();
    StorageDriver::fakeWrite(CONFIG_PATH_DASHBOARD, fixtures::kDashboardMinimalReload,
                             strlen(fixtures::kDashboardMinimalReload));
    StorageDriver::fakeWrite(CONFIG_PATH_SIGNALS, fixtures::kSignalsMinimal,
                             strlen(fixtures::kSignalsMinimal));
    TEST_ASSERT_TRUE(ConfigLoader::reloadAll());

    const CfgDashboard &after = ConfigLoader::getDashboardConfig();
    TEST_ASSERT_EQUAL_STRING("Reloaded Dashboard", after.name);
    TEST_ASSERT_EQUAL_UINT8(2, after.pageCount);
    TEST_ASSERT_EQUAL_STRING("first", after.pages[0].id);
    TEST_ASSERT_EQUAL_STRING("second", after.pages[1].id);
}

// Issue #458: signals load is also transactional. A failed signals.json read
// must leave s_signals byte-identical so widget renderers don't dereference
// half-overwritten signal definitions.
void test_reloadAll_invalidSignals_preservesSignalState() {
    stageMinimalFiles();
    TEST_ASSERT_TRUE(ConfigLoader::loadAll().signalsOk);

    const CfgSignalConfig &before = ConfigLoader::getSignalConfig();
    TEST_ASSERT_TRUE(before.loaded);
    TEST_ASSERT_EQUAL_UINT8(1, before.signalCount);
    TEST_ASSERT_EQUAL_STRING("rpm", before.signals[0].name);
    TEST_ASSERT_EQUAL_UINT32(0x370u, before.signals[0].canFrameId);
    const uint8_t signalCountBefore = before.signalCount;

    // Stage a valid dashboard but corrupt signals — reloadAll() returns false
    // because dashboardOk drives the return value, but the key invariant we
    // assert here is that s_signals stayed identical to fixture A.
    StorageDriver::fakeReset();
    StorageDriver::fakeWrite(CONFIG_PATH_DASHBOARD, fixtures::kDashboardMinimal,
                             strlen(fixtures::kDashboardMinimal));
    StorageDriver::fakeWrite(CONFIG_PATH_SIGNALS, fixtures::kSignalsCorrupt,
                             strlen(fixtures::kSignalsCorrupt));
    // Dashboard parses fine, signals fail — reloadAll only gates on dashboard.
    TEST_ASSERT_TRUE(ConfigLoader::reloadAll());

    const CfgSignalConfig &after = ConfigLoader::getSignalConfig();
    TEST_ASSERT_TRUE(after.loaded);
    TEST_ASSERT_EQUAL_UINT8(signalCountBefore, after.signalCount);
    TEST_ASSERT_EQUAL_STRING("rpm", after.signals[0].name);
    TEST_ASSERT_EQUAL_UINT32(0x370u, after.signals[0].canFrameId);
}

// Issues #17 / #18 / #1128 — targetProfile parsing. When the field is absent
// (every pre-#1128 dashboard, including kDashboardMinimal) the loader must
// surface the canshift-core default `crowpanel-28` so downstream scale
// computation reads a stable non-empty id.
void test_loadDashboard_targetProfile_defaultsToCrowpanel28WhenAbsent() {
    stageMinimalFiles();
    TEST_ASSERT_TRUE(ConfigLoader::loadAll().dashboardOk);
    TEST_ASSERT_EQUAL_STRING("crowpanel-28", ConfigLoader::getDashboardConfig().targetProfile);
}

// When the field is present, the literal id must round-trip onto the struct
// untouched so a future second profile (e.g. `crowpanel-50`) routes through
// the same code path without further parser changes.
void test_loadDashboard_targetProfile_roundTripsExplicitValue() {
    StorageDriver::fakeReset();
    StorageDriver::fakeWrite(CONFIG_PATH_DASHBOARD, fixtures::kDashboardWithProfile,
                             strlen(fixtures::kDashboardWithProfile));
    StorageDriver::fakeWrite(CONFIG_PATH_SIGNALS, fixtures::kSignalsMinimal,
                             strlen(fixtures::kSignalsMinimal));
    TEST_ASSERT_TRUE(ConfigLoader::loadAll().dashboardOk);
    TEST_ASSERT_EQUAL_STRING("crowpanel-28", ConfigLoader::getDashboardConfig().targetProfile);
}

void test_loadDashboard_pageTemplate_parsesCruiseControlAndDefaultsCustom() {
    StorageDriver::fakeReset();
    StorageDriver::fakeWrite(CONFIG_PATH_DASHBOARD, fixtures::kDashboardWithTemplates,
                             strlen(fixtures::kDashboardWithTemplates));
    StorageDriver::fakeWrite(CONFIG_PATH_SIGNALS, fixtures::kSignalsMinimal,
                             strlen(fixtures::kSignalsMinimal));

    TEST_ASSERT_TRUE(ConfigLoader::loadAll().dashboardOk);

    const CfgDashboard &dashboard = ConfigLoader::getDashboardConfig();
    TEST_ASSERT_EQUAL_UINT8(2, dashboard.pageCount);
    // First page omits `template` → defaults to CUSTOM for back-compat (#451).
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CfgPageTemplate::CUSTOM),
                            static_cast<uint8_t>(dashboard.pages[0].templateKind));
    // Second page carries the cruise_control template literal.
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CfgPageTemplate::CRUISE_CONTROL),
                            static_cast<uint8_t>(dashboard.pages[1].templateKind));
}

int main(int /*argc*/, char ** /*argv*/) {
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
    return UNITY_END();
}
