#pragma once

#include "config/config_loader.h"

namespace ConfigLoaderTest {

constexpr char kDashboardBakPath[] = "/config/dashboard.json.bak";

void stageMinimalFiles();
void stageDashboard(const char *primary, const char *backup);
bool errorStoreHolds(const char *code);
bool outcomeIs(ConfigLoader::DashboardOutcome expected, ConfigLoader::DashboardOutcome actual);

}

void test_loadSignals_malformedCanFrameId_dropsSignal();
void test_loadSignals_threeByteValue_isAccepted();
void test_loadSignals_nameTooLong_dropsSignal();
void test_loadSignals_unitTooLong_resetsUnitToEmpty();
void test_loadDashboard_belowGridFloor_reportsVersionTooOld();
void test_loadDashboard_atGridFloor_reportsNothing();
void test_loadDashboard_refusedWithBackup_restoresPrevious();
void test_loadDashboard_refusedWithoutBackup_isRefused();
void test_vetDashboard_refused_restoresFileAndKeepsLiveConfig();
void test_vetDashboard_valid_isLoadedAndKeepsLiveConfig();
void test_loadDashboard_unparsable_isUnreadable();
