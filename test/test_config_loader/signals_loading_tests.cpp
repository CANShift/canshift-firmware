#include "board_config.h"
#include "config/config_loader.h"
#include "diag/error_store.h"
#include "hal/storage/storage_driver.h"

#include "config_loader_test_support.h"
#include "fixtures/dashboard_minimal.json.h"
#include "fixtures/signals_minimal.json.h"

#include <string.h>
#include <unity.h>

void test_loadSignals_malformedCanFrameId_dropsSignal() {
    StorageDriver::fakeReset();
    StorageDriver::fakeWrite(kConfigPathDashboard, fixtures::kDashboardMinimal,
                             strlen(fixtures::kDashboardMinimal));
    StorageDriver::fakeWrite(kConfigPathSignals, fixtures::kSignalsMalformedCanFrameId,
                             strlen(fixtures::kSignalsMalformedCanFrameId));

    TEST_ASSERT_TRUE(ConfigLoader::loadAll().signalsOk);

    const CfgSignalConfig &signals = ConfigLoader::getSignalConfig();
    TEST_ASSERT_EQUAL_UINT8(1, signals.signalCount);
    TEST_ASSERT_EQUAL_STRING("rpm", signals.signals[0].name);
    TEST_ASSERT_EQUAL_UINT32(0x370u, signals.signals[0].canFrameId);
}

void test_loadSignals_threeByteValue_isAccepted() {
    StorageDriver::fakeReset();
    StorageDriver::fakeWrite(kConfigPathDashboard, fixtures::kDashboardMinimal,
                             strlen(fixtures::kDashboardMinimal));
    StorageDriver::fakeWrite(kConfigPathSignals, fixtures::kSignalsThreeByteValue,
                             strlen(fixtures::kSignalsThreeByteValue));

    TEST_ASSERT_TRUE(ConfigLoader::loadAll().signalsOk);

    const CfgSignalConfig &signals = ConfigLoader::getSignalConfig();
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
    TEST_ASSERT_EQUAL_UINT8(1, signals.signalCount);
    TEST_ASSERT_EQUAL_STRING("rpm", signals.signals[0].name);
}

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
    TEST_ASSERT_EQUAL_STRING("", signals.signals[0].unit);
}
