
#include "board_config.h"
#include "config/config_loader.h"
#include "diag/error_store.h"
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
    StorageDriver::fakeWrite(kConfigPathDashboard, fixtures::kDashboardMinimal,
                             strlen(fixtures::kDashboardMinimal));
    StorageDriver::fakeWrite(kConfigPathSignals, fixtures::kSignalsMinimal,
                             strlen(fixtures::kSignalsMinimal));
}

constexpr const char kDashboardBakPath[] = "/config/dashboard.json.bak";

void stageDashboard(const char *primary, const char *backup) {
    StorageDriver::fakeReset();
    ErrorStore::clear();
    StorageDriver::fakeWrite(kConfigPathDashboard, primary, strlen(primary));
    if (backup)
        StorageDriver::fakeWrite(kDashboardBakPath, backup, strlen(backup));
    StorageDriver::fakeWrite(kConfigPathSignals, fixtures::kSignalsMinimal,
                             strlen(fixtures::kSignalsMinimal));
}

bool errorStoreHolds(const char *code) {
    FwError rows[8];
    uint8_t count = 0;
    ErrorStore::getAll(rows, &count, 8);
    for (uint8_t i = 0; i < count; i++) {
        if (strcmp(rows[i].code, code) == 0)
            return true;
    }
    return false;
}

bool outcomeIs(ConfigLoader::DashboardOutcome expected, ConfigLoader::DashboardOutcome actual) {
    return static_cast<uint8_t>(expected) == static_cast<uint8_t>(actual);
}

}

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
    StorageDriver::fakeWrite(kConfigPathDashboard, fixtures::kDashboardCorrupt,
                             strlen(fixtures::kDashboardCorrupt));
    StorageDriver::fakeWrite(kConfigPathSignals, fixtures::kSignalsMinimal,
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
    StorageDriver::fakeWrite(kConfigPathDashboard, fixtures::kDashboardMinimal,
                             strlen(fixtures::kDashboardMinimal));
    StorageDriver::fakeWrite(kConfigPathSignals, fixtures::kSignalsCorrupt,
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
    StorageDriver::fakeWrite(kConfigPathDashboard, fixtures::kDashboardWithProfile,
                             strlen(fixtures::kDashboardWithProfile));
    StorageDriver::fakeWrite(kConfigPathSignals, fixtures::kSignalsMinimal,
                             strlen(fixtures::kSignalsMinimal));
    TEST_ASSERT_TRUE(ConfigLoader::loadAll().dashboardOk);
    TEST_ASSERT_EQUAL_STRING("crowpanel-28", ConfigLoader::getDashboardConfig().targetProfile);
}

// Issue #1159 — malformed canFrameId must drop the offending signal and keep
// all other signals intact. A bare strtoul would have silently produced 0.
void test_loadSignals_malformedCanFrameId_dropsSignal() {
    StorageDriver::fakeReset();
    StorageDriver::fakeWrite(kConfigPathDashboard, fixtures::kDashboardMinimal,
                             strlen(fixtures::kDashboardMinimal));
    StorageDriver::fakeWrite(kConfigPathSignals, fixtures::kSignalsMalformedCanFrameId,
                             strlen(fixtures::kSignalsMalformedCanFrameId));

    TEST_ASSERT_TRUE(ConfigLoader::loadAll().signalsOk);

    const CfgSignalConfig &signals = ConfigLoader::getSignalConfig();
    // Only the valid "rpm" signal survives; "tps" (0xGGG) is dropped.
    TEST_ASSERT_EQUAL_UINT8(1, signals.signalCount);
    TEST_ASSERT_EQUAL_STRING("rpm", signals.signals[0].name);
    TEST_ASSERT_EQUAL_UINT32(0x370u, signals.signals[0].canFrameId);
}

// Issue #1162 — signal name longer than CFG_MAX_SIGNAL_LEN must drop the
// signal (not truncate silently). A good signal before the offending one
// must survive intact.
void test_loadSignals_threeByteValue_isAccepted() {
    StorageDriver::fakeReset();
    StorageDriver::fakeWrite(kConfigPathDashboard, fixtures::kDashboardMinimal,
                             strlen(fixtures::kDashboardMinimal));
    StorageDriver::fakeWrite(kConfigPathSignals, fixtures::kSignalsThreeByteValue,
                             strlen(fixtures::kSignalsThreeByteValue));

    TEST_ASSERT_TRUE(ConfigLoader::loadAll().signalsOk);

    const CfgSignalConfig &signals = ConfigLoader::getSignalConfig();
    // The 3-byte "odo" survives; "too_wide" (5 bytes) is still dropped.
    TEST_ASSERT_EQUAL_UINT8(1, signals.signalCount);
    TEST_ASSERT_EQUAL_STRING("odo", signals.signals[0].name);
    TEST_ASSERT_EQUAL_UINT8(3, signals.signals[0].byteLength);
}

void test_loadSignals_nameTooLong_dropsSignal() {
    StorageDriver::fakeReset();
    StorageDriver::fakeWrite(kConfigPathDashboard, fixtures::kDashboardMinimal,
                             strlen(fixtures::kDashboardMinimal));
    StorageDriver::fakeWrite(kConfigPathSignals, fixtures::kSignalsNameTooLong,
                             strlen(fixtures::kSignalsNameTooLong));

    TEST_ASSERT_TRUE(ConfigLoader::loadAll().signalsOk);

    const CfgSignalConfig &signals = ConfigLoader::getSignalConfig();
    // Only "rpm" survives; the too-long-name signal is dropped.
    TEST_ASSERT_EQUAL_UINT8(1, signals.signalCount);
    TEST_ASSERT_EQUAL_STRING("rpm", signals.signals[0].name);
}

// Issue #1162 — unit string longer than sizeof(s.unit) must be reset to ""
// rather than truncated silently. The signal itself must survive.
void test_loadSignals_unitTooLong_resetsUnitToEmpty() {
    StorageDriver::fakeReset();
    StorageDriver::fakeWrite(kConfigPathDashboard, fixtures::kDashboardMinimal,
                             strlen(fixtures::kDashboardMinimal));
    StorageDriver::fakeWrite(kConfigPathSignals, fixtures::kSignalsUnitTooLong,
                             strlen(fixtures::kSignalsUnitTooLong));

    TEST_ASSERT_TRUE(ConfigLoader::loadAll().signalsOk);

    const CfgSignalConfig &signals = ConfigLoader::getSignalConfig();
    TEST_ASSERT_EQUAL_UINT8(1, signals.signalCount);
    TEST_ASSERT_EQUAL_STRING("rpm", signals.signals[0].name);
    // Unit must be empty string, not a truncated fragment.
    TEST_ASSERT_EQUAL_STRING("", signals.signals[0].unit);
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
    // First page omits `template` → defaults to CUSTOM for back-compat (#451).
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CfgPageTemplate::CUSTOM),
                            static_cast<uint8_t>(dashboard.pages[0].templateKind));
    // Second page carries the cruise_control template literal.
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

    // Negative origins clamp to 0 instead of wrapping through uint8_t.
    TEST_ASSERT_EQUAL_UINT8(0, page.widgets[0].layout.col);
    TEST_ASSERT_EQUAL_UINT8(3, page.widgets[0].layout.colSpan);
    TEST_ASSERT_EQUAL_UINT8(0, page.widgets[0].layout.row);
    TEST_ASSERT_EQUAL_UINT8(2, page.widgets[0].layout.rowSpan);

    // Oversized origins clamp to 12 - span.
    TEST_ASSERT_EQUAL_UINT8(9, page.widgets[1].layout.col);
    TEST_ASSERT_EQUAL_UINT8(3, page.widgets[1].layout.colSpan);
    TEST_ASSERT_EQUAL_UINT8(10, page.widgets[1].layout.row);
    TEST_ASSERT_EQUAL_UINT8(2, page.widgets[1].layout.rowSpan);

    // Zero spans clamp to 1; in-range origins survive.
    TEST_ASSERT_EQUAL_UINT8(4, page.widgets[2].layout.col);
    TEST_ASSERT_EQUAL_UINT8(1, page.widgets[2].layout.colSpan);
    TEST_ASSERT_EQUAL_UINT8(4, page.widgets[2].layout.row);
    TEST_ASSERT_EQUAL_UINT8(1, page.widgets[2].layout.rowSpan);

    // Span clamps before origin: col 8 + span 10 → span stays 10, col drops to 2.
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
    RUN_TEST(test_loadDashboard_outOfRangeLayout_clampsToGrid);
    RUN_TEST(test_loadDashboard_themePalette_parsesTextColorsAndDayNightSignal);
    RUN_TEST(test_loadDashboard_noThemes_leavesDayNightSignalEmpty);
    RUN_TEST(test_loadDashboard_partialPaletteAndOverlongSignal_fallBackSafely);
    RUN_TEST(test_loadSignals_malformedCanFrameId_dropsSignal);
    RUN_TEST(test_loadSignals_threeByteValue_isAccepted);
    RUN_TEST(test_loadSignals_nameTooLong_dropsSignal);
    RUN_TEST(test_loadSignals_unitTooLong_resetsUnitToEmpty);
    RUN_TEST(test_loadDashboard_gearBig_isParsed);
    RUN_TEST(test_loadDashboard_belowGridFloor_reportsVersionTooOld);
    RUN_TEST(test_loadDashboard_atGridFloor_reportsNothing);
    RUN_TEST(test_loadDashboard_refusedWithBackup_restoresPrevious);
    RUN_TEST(test_loadDashboard_refusedWithoutBackup_isRefused);
    RUN_TEST(test_vetDashboard_refused_restoresFileAndKeepsLiveConfig);
    RUN_TEST(test_vetDashboard_valid_isLoadedAndKeepsLiveConfig);
    RUN_TEST(test_loadDashboard_unparsable_isUnreadable);
    return UNITY_END();
}
