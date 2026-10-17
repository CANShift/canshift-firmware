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
  "criticalAlerts": [
    {"signal": "oil_press_bar", "firesAt": 1.0, "direction": "below", "acknowledgeOn": "dash"},
    {"signal": "oil_press_bar", "firesAt": 8.5, "direction": "above", "acknowledgeOn": "dash"}
  ],
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

constexpr const char *kDashboardLegacyAlert = R"({
  "version": "1.42.0",
  "name": "Legacy alert",
  "defaultPageId": "main",
  "revLimitRpm": 7000,
  "topBar": {"height": 24, "bgColor": "#111111", "textColor": "#FFFFFF"},
  "criticalAlert": {"signal": "oil_press_bar", "firesAt": 1.0, "direction": "below",
                    "acknowledgeOn": "dash"},
  "pages": [{"id": "main", "backgroundColor": "#000000", "showTopBar": true, "widgets": []}]
})";

constexpr const char *kDashboardTooManyAlerts = R"({
  "version": "1.43.0",
  "name": "Too many alerts",
  "defaultPageId": "main",
  "revLimitRpm": 7000,
  "topBar": {"height": 24, "bgColor": "#111111", "textColor": "#FFFFFF"},
  "criticalAlerts": [
    {"signal": "oil_press_bar", "firesAt": 1.0}, {"signal": "oil_press_bar", "firesAt": 2.0},
    {"signal": "oil_press_bar", "firesAt": 3.0}, {"signal": "oil_press_bar", "firesAt": 4.0},
    {"signal": "oil_press_bar", "firesAt": 5.0}
  ],
  "pages": [{"id": "main", "backgroundColor": "#000000", "showTopBar": true, "widgets": []}]
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

void assertLowOilPressureAlert(const CfgCriticalAlert &alert) {
    TEST_ASSERT_TRUE(alert.armed);
    TEST_ASSERT_EQUAL_STRING("oil_press_bar", alert.signalId);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, alert.firesAt);
    TEST_ASSERT_TRUE(alert.below);
}

void test_criticalAlerts_armEveryEntryInOrder() {
    const CfgDashboard &dashboard = load(kDashboard);
    TEST_ASSERT_EQUAL_UINT8(2, dashboard.criticalAlertCount);
    assertLowOilPressureAlert(dashboard.criticalAlerts[0]);
    TEST_ASSERT_TRUE(dashboard.criticalAlerts[1].armed);
    TEST_ASSERT_EQUAL_FLOAT(8.5f, dashboard.criticalAlerts[1].firesAt);
    TEST_ASSERT_FALSE(dashboard.criticalAlerts[1].below);
}

void test_legacyCriticalAlert_isReadAsAOneEntryList() {
    const CfgDashboard &dashboard = load(kDashboardLegacyAlert);
    TEST_ASSERT_EQUAL_UINT8(1, dashboard.criticalAlertCount);
    assertLowOilPressureAlert(dashboard.criticalAlerts[0]);
}

void test_criticalAlerts_pastTheCap_keepTheFirstEntries() {
    const CfgDashboard &dashboard = load(kDashboardTooManyAlerts);
    TEST_ASSERT_EQUAL_UINT8(kConfigMaxCriticalAlerts, dashboard.criticalAlertCount);
    TEST_ASSERT_EQUAL_FLOAT(4.0f, dashboard.criticalAlerts[kConfigMaxCriticalAlerts - 1].firesAt);
}

void test_criticalAlerts_absent_armNothing() {
    TEST_ASSERT_EQUAL_UINT8(0, load(kDashboardNoAlert).criticalAlertCount);
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_arcGauge_readsKickerAndWarnLevel);
    RUN_TEST(test_numericGauge_withoutWarnLevel_leavesItUnset);
    RUN_TEST(test_gear_readsKicker);
    RUN_TEST(test_criticalAlerts_armEveryEntryInOrder);
    RUN_TEST(test_legacyCriticalAlert_isReadAsAOneEntryList);
    RUN_TEST(test_criticalAlerts_pastTheCap_keepTheFirstEntries);
    RUN_TEST(test_criticalAlerts_absent_armNothing);
    return UNITY_END();
}
