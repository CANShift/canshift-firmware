#include "board_config.h"
#include "config/config_loader.h"
#include "hal/storage/storage_driver.h"
#include "ui/status_field_text.h"

#include "fixtures/status_row_dashboards.json.h"

#include <string.h>
#include <unity.h>

namespace {

constexpr size_t kBufCap = 16;

void stageDashboard(const char *json) {
    StorageDriver::fakeReset();
    StorageDriver::fakeWrite(kConfigPathDashboard, json, strlen(json));
    const ConfigLoader::LoadResult result = ConfigLoader::loadAll();
    TEST_ASSERT_TRUE(result.dashboardOk);
}

CfgTopBarItem valueItem(const char *unit, bool showUnit) {
    CfgTopBarItem item{};
    item.kind = TopBarItemKind::SIGNAL;
    strlcpy(item.unit, unit, sizeof(item.unit));
    item.showUnit = showUnit;
    return item;
}

CfgTopBarItem timerItem(CfgTimerSource source, const char *prefix) {
    CfgTopBarItem item{};
    item.kind = TopBarItemKind::TIMER;
    item.timerSource = source;
    strlcpy(item.text, prefix, sizeof(item.text));
    return item;
}

CfgTopBarItem flagItem() {
    CfgTopBarItem item{};
    item.kind = TopBarItemKind::FLAG_WORD;
    strlcpy(item.text, "ARMED", sizeof(item.text));
    strlcpy(item.offText, "LAUNCH OFF", sizeof(item.offText));
    return item;
}

TimerSources::Inputs trackInputs() {
    TrackStore::State track{};
    track.bestLapMs = 96080;
    track.lapNumber = 4;
    return TimerSources::select(TimerService::Snapshot{}, track, true);
}

}

void setUp() {}
void tearDown() {}

void test_value_withoutUnit_isNeverConverted() {
    stageDashboard(fixtures::kStatusRowImperial);
    char buf[kBufCap];
    StatusFieldText::formatValue(buf, sizeof(buf), 128.0f, "TRIP %.0f km", valueItem("", false));
    TEST_ASSERT_EQUAL_STRING("TRIP 128 km", buf);
}

void test_value_withUnit_convertsUnderImperial() {
    stageDashboard(fixtures::kStatusRowImperial);
    char buf[kBufCap];
    StatusFieldText::formatValue(buf, sizeof(buf), 128.0f, "%.0f", valueItem("°C", false));
    TEST_ASSERT_EQUAL_STRING("262", buf);
}

void test_value_showUnit_appendsTheImperialSymbol() {
    stageDashboard(fixtures::kStatusRowImperial);
    char buf[kBufCap];
    StatusFieldText::formatValue(buf, sizeof(buf), 128.0f, "TRIP %.0f", valueItem("km", true));
    TEST_ASSERT_EQUAL_STRING("TRIP 80 mi", buf);
}

void test_value_showUnit_keepsTheMetricSymbol() {
    stageDashboard(fixtures::kStatusRowMetric);
    char buf[kBufCap];
    StatusFieldText::formatValue(buf, sizeof(buf), 128.0f, "TRIP %.0f", valueItem("km", true));
    TEST_ASSERT_EQUAL_STRING("TRIP 128 km", buf);
}

void test_value_unpairedUnit_isUntouched() {
    stageDashboard(fixtures::kStatusRowImperial);
    char buf[kBufCap];
    StatusFieldText::formatValue(buf, sizeof(buf), 13.8f, "%.1f", valueItem("V", true));
    TEST_ASSERT_EQUAL_STRING("13.8 V", buf);
}

void test_value_convertedPressure_dropsOneDecimal() {
    stageDashboard(fixtures::kStatusRowImperial);
    char buf[kBufCap];
    StatusFieldText::formatValue(buf, sizeof(buf), 1.61f, "%.2f", valueItem("bar", false));
    TEST_ASSERT_EQUAL_STRING("23.4", buf);
}

void test_value_metricPressure_keepsTheSpecDecimals() {
    stageDashboard(fixtures::kStatusRowMetric);
    char buf[kBufCap];
    StatusFieldText::formatValue(buf, sizeof(buf), 1.61f, "%.2f", valueItem("bar", false));
    TEST_ASSERT_EQUAL_STRING("1.61", buf);
}

void test_withPrefix_joinsWithOneSpace() {
    char buf[kBufCap];
    StatusFieldText::withPrefix(buf, sizeof(buf), "MAX OIL", "128");
    TEST_ASSERT_EQUAL_STRING("MAX OIL 128", buf);
    StatusFieldText::withPrefix(buf, sizeof(buf), "", "128");
    TEST_ASSERT_EQUAL_STRING("128", buf);
}

void test_timer_lapCount_readsTheTrackLapNumber() {
    char buf[kBufCap];
    TEST_ASSERT_TRUE(StatusFieldText::formatTimer(
        buf, sizeof(buf), timerItem(CfgTimerSource::LapCount, "LAP"), trackInputs()));
    TEST_ASSERT_EQUAL_STRING("LAP 4", buf);
}

void test_timer_best_formatsTheBestLap() {
    char buf[kBufCap];
    TEST_ASSERT_TRUE(StatusFieldText::formatTimer(
        buf, sizeof(buf), timerItem(CfgTimerSource::Best, "BEST"), trackInputs()));
    TEST_ASSERT_EQUAL_STRING("BEST 1:36.08", buf);
}

void test_timer_bestWithoutALap_isUnknown() {
    char buf[kBufCap];
    const TimerSources::Inputs none =
        TimerSources::select(TimerService::Snapshot{}, TrackStore::State{}, false);
    TEST_ASSERT_FALSE(StatusFieldText::formatTimer(buf, sizeof(buf),
                                                   timerItem(CfgTimerSource::Best, "BEST"), none));
    TEST_ASSERT_EQUAL_STRING("BEST --:--", buf);
}

void test_flagWord_readsTheOnAndOffWords() {
    TEST_ASSERT_EQUAL_STRING("ARMED", StatusFieldText::flagWord(true, 1.0f, flagItem()));
    TEST_ASSERT_EQUAL_STRING("LAUNCH OFF", StatusFieldText::flagWord(true, 0.0f, flagItem()));
}

void test_flagWord_invalidSignal_isStaleNotOff() {
    TEST_ASSERT_EQUAL_STRING(StatusFieldText::kStalePlaceholder,
                             StatusFieldText::flagWord(false, 0.0f, flagItem()));
}

void test_loader_parsesTimerItems() {
    stageDashboard(fixtures::kStatusRowImperial);
    const CfgStatusRow &row = ConfigLoader::getDashboardConfig().pages[0].statusRow;
    TEST_ASSERT_TRUE(row.hasCenter && row.hasRight);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TopBarItemKind::TIMER),
                            static_cast<uint8_t>(row.center.kind));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CfgTimerSource::LapCount),
                            static_cast<uint8_t>(row.center.timerSource));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CfgTimerSource::Best),
                            static_cast<uint8_t>(row.right.timerSource));
}

void test_loader_parsesFlagWordAndUnitFields() {
    stageDashboard(fixtures::kStatusRowImperial);
    const CfgStatusRow &row = ConfigLoader::getDashboardConfig().pages[1].statusRow;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TopBarItemKind::FLAG_WORD),
                            static_cast<uint8_t>(row.center.kind));
    TEST_ASSERT_EQUAL_STRING("ALS OFF", row.center.offText);
    TEST_ASSERT_EQUAL_STRING("km", row.right.unit);
    TEST_ASSERT_TRUE(row.right.showUnit);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_value_withoutUnit_isNeverConverted);
    RUN_TEST(test_value_withUnit_convertsUnderImperial);
    RUN_TEST(test_value_showUnit_appendsTheImperialSymbol);
    RUN_TEST(test_value_showUnit_keepsTheMetricSymbol);
    RUN_TEST(test_value_unpairedUnit_isUntouched);
    RUN_TEST(test_value_convertedPressure_dropsOneDecimal);
    RUN_TEST(test_value_metricPressure_keepsTheSpecDecimals);
    RUN_TEST(test_withPrefix_joinsWithOneSpace);
    RUN_TEST(test_timer_lapCount_readsTheTrackLapNumber);
    RUN_TEST(test_timer_best_formatsTheBestLap);
    RUN_TEST(test_timer_bestWithoutALap_isUnknown);
    RUN_TEST(test_flagWord_readsTheOnAndOffWords);
    RUN_TEST(test_flagWord_invalidSignal_isStaleNotOff);
    RUN_TEST(test_loader_parsesTimerItems);
    RUN_TEST(test_loader_parsesFlagWordAndUnitFields);
    return UNITY_END();
}
