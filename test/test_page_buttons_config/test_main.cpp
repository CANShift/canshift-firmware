#include "board_config.h"
#include "config/config_loader.h"
#include "hal/storage/storage_driver.h"

#include <string.h>
#include <unity.h>

namespace {

constexpr const char *kSignals = R"({
  "version": "1.42.0",
  "protocol": "custom_v1.0",
  "canSpeedKbps": 500,
  "signals": []
})";

constexpr const char *kDashboardWithButtons = R"({
  "version": "1.42.0",
  "name": "Buttons",
  "defaultPageId": "main",
  "revLimitRpm": 7000,
  "topBar": {"height": 24, "bgColor": "#111111", "textColor": "#FFFFFF"},
  "pageButtons": {"next": "flag_cruise", "previous": "flag_pit_limit"},
  "pages": [{"id": "main", "backgroundColor": "#000000", "showTopBar": true, "widgets": []}]
})";

constexpr const char *kDashboardNextOnly = R"({
  "version": "1.42.0",
  "name": "Next only",
  "defaultPageId": "main",
  "revLimitRpm": 7000,
  "topBar": {"height": 24, "bgColor": "#111111", "textColor": "#FFFFFF"},
  "pageButtons": {"next": "flag_cruise"},
  "pages": [{"id": "main", "backgroundColor": "#000000", "showTopBar": true, "widgets": []}]
})";

constexpr const char *kDashboardWithoutButtons = R"({
  "version": "1.42.0",
  "name": "No buttons",
  "defaultPageId": "main",
  "revLimitRpm": 7000,
  "topBar": {"height": 24, "bgColor": "#111111", "textColor": "#FFFFFF"},
  "pages": [{"id": "main", "backgroundColor": "#000000", "showTopBar": true, "widgets": []}]
})";

constexpr const char *kDashboardOverlongButton = R"({
  "version": "1.42.0",
  "name": "Overlong",
  "defaultPageId": "main",
  "revLimitRpm": 7000,
  "topBar": {"height": 24, "bgColor": "#111111", "textColor": "#FFFFFF"},
  "pageButtons": {"next": "a_signal_name_that_is_far_too_long_to_fit_in_the_firmware_signal_slot",
                  "previous": "flag_pit_limit"},
  "pages": [{"id": "main", "backgroundColor": "#000000", "showTopBar": true, "widgets": []}]
})";

const CfgPageButtons &load(const char *dashboard) {
    StorageDriver::fakeReset();
    StorageDriver::fakeWrite(kConfigPathDashboard, dashboard, strlen(dashboard));
    StorageDriver::fakeWrite(kConfigPathSignals, kSignals, strlen(kSignals));
    TEST_ASSERT_TRUE(ConfigLoader::loadAll().dashboardOk);
    return ConfigLoader::getDashboardConfig().pageButtons;
}

}

void setUp() {}
void tearDown() {}

void test_pageButtons_readsBothSignals() {
    const CfgPageButtons &buttons = load(kDashboardWithButtons);
    TEST_ASSERT_EQUAL_STRING("flag_cruise", buttons.next);
    TEST_ASSERT_EQUAL_STRING("flag_pit_limit", buttons.previous);
}

void test_pageButtons_missingButtonStaysEmpty() {
    const CfgPageButtons &buttons = load(kDashboardNextOnly);
    TEST_ASSERT_EQUAL_STRING("flag_cruise", buttons.next);
    TEST_ASSERT_EQUAL_STRING("", buttons.previous);
}

void test_pageButtons_absentBindsNothing() {
    const CfgPageButtons &buttons = load(kDashboardWithoutButtons);
    TEST_ASSERT_EQUAL_STRING("", buttons.next);
    TEST_ASSERT_EQUAL_STRING("", buttons.previous);
}

void test_pageButtons_overlongSignalIsDroppedNotTruncated() {
    const CfgPageButtons &buttons = load(kDashboardOverlongButton);
    TEST_ASSERT_EQUAL_STRING("", buttons.next);
    TEST_ASSERT_EQUAL_STRING("flag_pit_limit", buttons.previous);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_pageButtons_readsBothSignals);
    RUN_TEST(test_pageButtons_missingButtonStaysEmpty);
    RUN_TEST(test_pageButtons_absentBindsNothing);
    RUN_TEST(test_pageButtons_overlongSignalIsDroppedNotTruncated);
    return UNITY_END();
}
