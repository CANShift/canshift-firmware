#include "board_config.h"
#include "can/can_parser.h"
#include "config/config_loader.h"
#include "hal/storage/storage_driver.h"
#include "runtime/signal_store.h"

#include <string.h>
#include <unity.h>

namespace {

constexpr uint32_t kObdResponseId = 0x7E8;
constexpr uint32_t kBroadcastId = 0x370;
constexpr uint8_t kNegativeResponse[] = {0x03, 0x7F, 0x01, 0x12, 0x00, 0x00, 0x00, 0x00};
constexpr uint8_t kBroadcastFrame[] = {0x00, 0x8C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
constexpr float kPolledRpm = 3000.0f;
constexpr float kEpsilon = 1e-3f;

constexpr const char *kObdSignals = R"json({
  "version": "1.0.0",
  "signals": [
    {"name": "rpm", "canFrameId": "0x7E8", "startByte": 3, "byteLength": 2, "scale": 0.25,
     "targetId": 7, "polling": {"mode": 1, "pid": 12, "intervalMs": 250}},
    {"name": "speed_kph", "canFrameId": "0x7E8", "startByte": 3, "byteLength": 1,
     "polling": {"mode": 1, "pid": 13, "intervalMs": 500}},
    {"name": "battery_volts", "canFrameId": "0x370", "startByte": 0, "byteLength": 2,
     "scale": 0.1},
    {"name": "gear", "canFrameId": "0x370", "startByte": 0, "byteLength": 1,
     "expr": "ID7/1000", "exprRefs": [7]}
  ]
})json";

void loadObdSignals() {
    StorageDriver::fakeReset();
    StorageDriver::fakeWrite(kConfigPathSignals, kObdSignals, strlen(kObdSignals));
    TEST_ASSERT_TRUE(ConfigLoader::loadAll().signalsOk);
    SignalStore::init();
    CanParser::loadSignalDefinitions();
}

}

void setUp() {
    loadObdSignals();
}

void tearDown() {}

void test_unmatchedObdResponseDoesNotDecodeIntoPolledSignals() {
    CanParser::parseFrame(kObdResponseId, kNegativeResponse, sizeof(kNegativeResponse));
    TEST_ASSERT_FALSE(SignalStore::isValid(SignalIds::kRpm));
    TEST_ASSERT_FALSE(SignalStore::isValid(SignalIds::kSpeedKph));
}

void test_polledValueSurvivesAnUnmatchedObdResponse() {
    SignalStore::update(SignalIds::kRpm, kPolledRpm);
    CanParser::parseFrame(kObdResponseId, kNegativeResponse, sizeof(kNegativeResponse));
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, kPolledRpm, SignalStore::read(SignalIds::kRpm));
}

void test_broadcastSignalsStillDecode() {
    CanParser::parseFrame(kBroadcastId, kBroadcastFrame, sizeof(kBroadcastFrame));
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, 14.0f, SignalStore::read(SignalIds::kBatteryVolts));
}

void test_expressionsStillReadAPolledSignalByTargetId() {
    SignalStore::update(SignalIds::kRpm, kPolledRpm);
    CanParser::parseFrame(kBroadcastId, kBroadcastFrame, sizeof(kBroadcastFrame));
    TEST_ASSERT_TRUE(SignalStore::isValid(SignalIds::kGear));
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, kPolledRpm / 1000.0f, SignalStore::read(SignalIds::kGear));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_unmatchedObdResponseDoesNotDecodeIntoPolledSignals);
    RUN_TEST(test_polledValueSurvivesAnUnmatchedObdResponse);
    RUN_TEST(test_broadcastSignalsStillDecode);
    RUN_TEST(test_expressionsStillReadAPolledSignalByTargetId);
    return UNITY_END();
}
