#include "board_config.h"
#include "config/config_loader.h"
#include "hal/storage/storage_driver.h"

#include <cmath>
#include <string.h>
#include <unity.h>

namespace {

constexpr const char *kSignals = R"({
  "version": "1.41.0",
  "protocol": "custom_v1.0",
  "canSpeedKbps": 500,
  "signals": [
    {"name": "oil_press_bar", "canFrameId": "0x370", "startByte": 0, "byteLength": 2,
     "bigEndian": true, "signed": false, "scale": 0.01, "offset": 0, "unit": "bar",
     "min": 0, "max": 10, "timeoutMs": 500}
  ]
})";

constexpr const char *kDashboard = R"({
  "version": "1.41.0",
  "name": "Thresholds",
  "defaultPageId": "main",
  "revLimitRpm": 7000,
  "topBar": {"height": 24, "bgColor": "#111111", "textColor": "#FFFFFF"},
  "criticalAlert": {"signal": "oil_press_bar", "firesAt": 1.0, "direction": "below",
                    "acknowledgeOn": "dash"},
  "pages": [
    {
      "id": "main",
      "backgroundColor": "#000000",
      "showTopBar": true,
      "widgets": [
        {"id": "arc", "type": "gauge", "signal": "oil_press_bar",
         "layout": {"col": 0, "colSpan": 4, "row": 0, "rowSpan": 4, "zOrder": 0},
         "config": {"type": "gauge", "displayStyle": "arc", "minValue": 0, "maxValue": 10,
                    "warnLevel": 1.5, "dangerLevel": 1.1, "dangerBelow": true,
                    "decimalPlaces": 1, "kicker": "Oil"}},
        {"id": "num", "type": "gauge", "signal": "oil_press_bar",
         "layout": {"col": 4, "colSpan": 4, "row": 0, "rowSpan": 4, "zOrder": 0},
         "config": {"type": "gauge", "displayStyle": "numeric", "minValue": 0, "maxValue": 10,
                    "dangerLevel": 1.1, "decimalPlaces": 1}},
        {"id": "gear", "type": "gear", "signal": "gear",
         "layout": {"col": 8, "colSpan": 4, "row": 0, "rowSpan": 4, "zOrder": 0},
         "config": {"type": "gear", "decimalPlaces": 0, "kicker": "BOX"}}
      ]
    }
  ]
})";

constexpr const char *kDashboardNoAlert = R"({
  "version": "1.41.0",
  "name": "No alert",
  "defaultPageId": "main",
  "revLimitRpm": 7000,
  "topBar": {"height": 24, "bgColor": "#111111", "textColor": "#FFFFFF"},
  "pages": [{"id": "main", "backgroundColor": "#000000", "showTopBar": true, "widgets": []}]
})";

const CfgDashboard &load(const char *dashboard) {
    StorageDriver::fakeReset();
    StorageDriver::fakeWrite(kConfigPathDashboard, dashboard, strlen(dashboard));
    StorageDriver::fakeWrite(kConfigPathSignals, kSignals, strlen(kSignals));
    TEST_ASSERT_TRUE(ConfigLoader::loadAll().dashboardOk);
    return ConfigLoader::getDashboardConfig();
}

const CfgWidget &widgetAt(const CfgDashboard &dashboard, uint8_t index) {
    TEST_ASSERT_TRUE(dashboard.pages[0].widgetCount > index);
    return dashboard.pages[0].widgets[index];
}

} // namespace

void setUp() {}
void tearDown() {}

void test_arcGauge_readsKickerAndWarnLevel() {
    const CfgWidget &arc = widgetAt(load(kDashboard), 0);
    TEST_ASSERT_EQUAL_STRING("Oil", arc.gauge.kicker);
    TEST_ASSERT_EQUAL_FLOAT(1.5f, arc.gauge.warnLevel);
    TEST_ASSERT_TRUE(arc.gauge.dangerBelow);
}

void test_numericGauge_withoutWarnLevel_leavesItUnset() {
    const CfgWidget &num = widgetAt(load(kDashboard), 1);
    TEST_ASSERT_TRUE(std::isnan(num.label.warnLevel));
    TEST_ASSERT_EQUAL_STRING("", num.label.kicker);
}

void test_gear_readsKicker() {
    const CfgWidget &gear = widgetAt(load(kDashboard), 2);
    TEST_ASSERT_EQUAL_STRING("BOX", gear.label.kicker);
}

void test_criticalAlert_isArmedFromTheDashboard() {
    const CfgCriticalAlert &alert = load(kDashboard).criticalAlert;
    TEST_ASSERT_TRUE(alert.armed);
    TEST_ASSERT_EQUAL_STRING("oil_press_bar", alert.signalId);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, alert.firesAt);
    TEST_ASSERT_TRUE(alert.below);
}

void test_criticalAlert_absent_armsNothing() {
    const CfgCriticalAlert &alert = load(kDashboardNoAlert).criticalAlert;
    TEST_ASSERT_FALSE(alert.armed);
    TEST_ASSERT_TRUE(std::isnan(alert.firesAt));
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_arcGauge_readsKickerAndWarnLevel);
    RUN_TEST(test_numericGauge_withoutWarnLevel_leavesItUnset);
    RUN_TEST(test_gear_readsKicker);
    RUN_TEST(test_criticalAlert_isArmedFromTheDashboard);
    RUN_TEST(test_criticalAlert_absent_armsNothing);
    return UNITY_END();
}
