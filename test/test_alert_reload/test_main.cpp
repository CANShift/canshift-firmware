#include "board_config.h"
#include "config/config_loader.h"
#include "hal/storage/storage_driver.h"
#include "runtime/alert_engine.h"
#include "runtime/critical_alert_source.h"
#include "runtime/signal_store.h"

#include <Arduino.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <unity.h>

namespace {

constexpr const char *kCoolantLimits = "\"warningLevel\": 100,\n      \"dangerLevel\": 115,";
constexpr const char *kCriticalAlert =
    "\"criticalAlert\": {\"signal\": \"oil_temp_c\", \"firesAt\": 130},";
constexpr float kBetweenDefaultAndConfiguredC = 112.0f;
constexpr uint32_t kTickStepMs = 50;

std::string readFile(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return {};
    std::string out;
    char chunk[4096];
    size_t got = 0;
    while ((got = fread(chunk, 1, sizeof(chunk), f)) > 0)
        out.append(chunk, got);
    fclose(f);
    return out;
}

std::string without(std::string text, const char *needle) {
    const size_t at = text.find(needle);
    if (at != std::string::npos)
        text.erase(at, strlen(needle));
    return text;
}

std::string withCriticalAlert(std::string dashboard) {
    dashboard.insert(dashboard.find('{') + 1, kCriticalAlert);
    return dashboard;
}

void loadConfig(const std::string &dashboard, const std::string &signals) {
    StorageDriver::fakeReset();
    StorageDriver::fakeWrite(kConfigPathDashboard, dashboard.c_str(), dashboard.size());
    StorageDriver::fakeWrite(kConfigPathSignals, signals.c_str(), signals.size());
    TEST_ASSERT_TRUE(ConfigLoader::reloadAll());
    AlertEngine::init();
}

AlertEngine::AlertLevel coolantLevelAt(float tempC) {
    SignalStore::update(SignalIds::kCoolantTempC, tempC);
    SignalStore::SignalValue snap[kSignalStoreMaxSignals];
    SignalStore::snapshotAll(snap);
    mockSetMillis(millis() + kTickStepMs);
    AlertEngine::tick(snap);
    return AlertEngine::getState().coolantTemp;
}

std::string s_dashboard;
std::string s_signals;

}

void setUp() {
    mockSetMillis(0);
    SignalStore::init();
    s_dashboard = readFile("data/config/dashboard.json");
    s_signals = readFile("data/config/signals.json");
    TEST_ASSERT_FALSE(s_dashboard.empty());
    TEST_ASSERT_NOT_EQUAL(std::string::npos, s_signals.find(kCoolantLimits));
}

void tearDown() {}

void test_a_threshold_removed_by_a_reload_falls_back_to_the_default() {
    loadConfig(s_dashboard, s_signals);
    TEST_ASSERT_EQUAL(AlertEngine::AlertLevel::WARNING,
                      coolantLevelAt(kBetweenDefaultAndConfiguredC));

    loadConfig(s_dashboard, without(s_signals, kCoolantLimits));
    TEST_ASSERT_EQUAL(AlertEngine::AlertLevel::CRITICAL,
                      coolantLevelAt(kBetweenDefaultAndConfiguredC));
}

void test_a_held_critical_does_not_survive_a_reload() {
    loadConfig(s_dashboard, without(s_signals, kCoolantLimits));
    TEST_ASSERT_EQUAL(AlertEngine::AlertLevel::CRITICAL,
                      coolantLevelAt(kBetweenDefaultAndConfiguredC));

    loadConfig(s_dashboard, s_signals);
    TEST_ASSERT_EQUAL(AlertEngine::AlertLevel::NORMAL, AlertEngine::getState().coolantTemp);
    TEST_ASSERT_EQUAL(AlertEngine::AlertLevel::NORMAL, AlertEngine::getState().global);
}

void test_a_critical_alert_armed_by_a_reload_is_armed() {
    loadConfig(s_dashboard, s_signals);
    TEST_ASSERT_FALSE(CriticalAlertSource::armed(0).armed);

    loadConfig(withCriticalAlert(s_dashboard), s_signals);
    TEST_ASSERT_TRUE(CriticalAlertSource::armed(0).armed);
    TEST_ASSERT_EQUAL(SignalIds::kOilTempC, CriticalAlertSource::armed(0).id);

    loadConfig(s_dashboard, s_signals);
    TEST_ASSERT_FALSE(CriticalAlertSource::armed(0).armed);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_a_threshold_removed_by_a_reload_falls_back_to_the_default);
    RUN_TEST(test_a_held_critical_does_not_survive_a_reload);
    RUN_TEST(test_a_critical_alert_armed_by_a_reload_is_armed);
    return UNITY_END();
}
