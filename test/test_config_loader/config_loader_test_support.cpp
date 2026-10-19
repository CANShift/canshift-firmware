#include "board_config.h"
#include "config/config_loader.h"
#include "diag/error_store.h"
#include "hal/storage/storage_driver.h"

#include "config_loader_test_support.h"
#include "fixtures/dashboard_minimal.json.h"
#include "fixtures/signals_minimal.json.h"

#include <string.h>
#include <unity.h>

namespace ConfigLoaderTest {

void stageMinimalFiles() {
    StorageDriver::fakeReset();
    StorageDriver::fakeWrite(kConfigPathDashboard, fixtures::kDashboardMinimal,
                             strlen(fixtures::kDashboardMinimal));
    StorageDriver::fakeWrite(kConfigPathSignals, fixtures::kSignalsMinimal,
                             strlen(fixtures::kSignalsMinimal));
}

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
