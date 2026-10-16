#include "board_config.h"
#include "config/config_loader.h"
#include "config/signals_source.h"
#include "hal/storage/storage_driver.h"

#include <string.h>
#include <string>
#include <unity.h>

namespace {

using SignalsSource::Source;
using SignalsSource::Verdict;

constexpr const char *kUploadPath = "/config/upload.json";
constexpr const char *kSignalsBakPath = "/config/signals.json.bak";

constexpr const char *kRpmTable = R"({"version":"1.0.0","signals":[
  {"name":"rpm","canFrameId":"0x370","startByte":0,"byteLength":2,"timeoutMs":1000}]})";

constexpr const char *kBoostTable = R"({"version":"1.0.0","signals":[
  {"name":"boost","canFrameId":"0x371","startByte":0,"byteLength":2,"timeoutMs":1000}]})";

constexpr const char *kTruncated = R"({"version":"1.0.0","signals":[{"name":"rpm")";

constexpr const char *kNoSignalList = R"({"version":"1.0.0","signals":"rpm"})";

constexpr const char *kOneBadSignal = R"({"version":"1.0.0","signals":[
  {"name":"rpm","canFrameId":"0x370","startByte":0,"byteLength":2,"timeoutMs":1000},
  {"name":"tps","canFrameId":"0xGGG","startByte":0,"byteLength":1,"timeoutMs":1000}]})";

constexpr uint8_t kVerdictCount = 5;
constexpr Verdict kAllVerdicts[kVerdictCount] = {Verdict::Ok, Verdict::Unreadable,
                                                 Verdict::NoSignalList, Verdict::TooMany,
                                                 Verdict::SignalRejected};

void stage(const char *path, const char *json) {
    StorageDriver::fakeWrite(path, json, strlen(json));
}

std::string tableOf(int count) {
    std::string json = R"({"version":"1.0.0","signals":[)";
    for (int i = 0; i < count; ++i) {
        if (i > 0)
            json += ",";
        json += R"({"name":"s)" + std::to_string(i) +
                R"(","canFrameId":"0x370","startByte":0,"byteLength":1,"timeoutMs":1000})";
    }
    return json + "]}";
}

uint8_t inspect(const char *json) {
    stage(kUploadPath, json);
    return static_cast<uint8_t>(ConfigLoader::inspectSignalsFile(kUploadPath));
}

bool fileHolds(const char *path, const char *json) {
    size_t length = 0;
    const uint8_t *data = StorageDriver::fakeRead(path, &length);
    return data != nullptr && length == strlen(json) && memcmp(data, json, length) == 0;
}

const char *firstSignalName() {
    return ConfigLoader::getSignalConfig().signals[0].name;
}

bool acceptSignals(const char *stagedPath) {
    return ConfigLoader::inspectSignalsFile(stagedPath) == Verdict::Ok;
}

StorageDriver::ChunkCommit upload(const char *json) {
    TEST_ASSERT_TRUE(StorageDriver::beginChunkedWriteAtomic(kConfigPathSignals));
    TEST_ASSERT_TRUE(
        StorageDriver::appendChunk(reinterpret_cast<const uint8_t *>(json), strlen(json)));
    return StorageDriver::endChunkedWriteChecked(acceptSignals);
}

} // namespace

void setUp() {
    StorageDriver::fakeReset();
}

void tearDown() {}

void test_aCleanTableIsAccepted() {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Verdict::Ok), inspect(kRpmTable));
}

void test_brokenJsonIsUnreadable() {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Verdict::Unreadable), inspect(kTruncated));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Verdict::Unreadable),
                            static_cast<uint8_t>(ConfigLoader::inspectSignalsFile("/none.json")));
}

void test_aMissingSignalListIsRejected() {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Verdict::NoSignalList), inspect(kNoSignalList));
}

void test_aSignalTheLoaderWouldDropIsRejected() {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Verdict::SignalRejected), inspect(kOneBadSignal));
}

void test_aTablePastTheCapIsRejected() {
    const std::string full = tableOf(kConfigMaxSignals);
    const std::string over = tableOf(kConfigMaxSignals + 1);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Verdict::Ok), inspect(full.c_str()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Verdict::TooMany), inspect(over.c_str()));
}

void test_inspectingLeavesTheLiveTableAlone() {
    stage(kConfigPathSignals, kRpmTable);
    TEST_ASSERT_TRUE(ConfigLoader::loadAll().signalsOk);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Verdict::Ok), inspect(kBoostTable));
    TEST_ASSERT_EQUAL_STRING("rpm", firstSignalName());
}

void test_aCleanPrimaryAlwaysWins() {
    for (Verdict backup : kAllVerdicts)
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Source::Primary),
                                static_cast<uint8_t>(SignalsSource::choose(Verdict::Ok, backup)));
}

void test_aCleanBackupBeatsAFlawedPrimary() {
    for (Verdict primary : kAllVerdicts) {
        if (primary == Verdict::Ok)
            continue;
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Source::Backup),
                                static_cast<uint8_t>(SignalsSource::choose(primary, Verdict::Ok)));
    }
}

void test_aDegradedTableBeatsNothing() {
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(Source::Primary),
        static_cast<uint8_t>(SignalsSource::choose(Verdict::SignalRejected, Verdict::Unreadable)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(Source::Backup),
        static_cast<uint8_t>(SignalsSource::choose(Verdict::NoSignalList, Verdict::TooMany)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(Source::None),
        static_cast<uint8_t>(SignalsSource::choose(Verdict::Unreadable, Verdict::NoSignalList)));
}

void test_bootFallsBackToTheBackupAndPromotesIt() {
    stage(kConfigPathSignals, kNoSignalList);
    stage(kSignalsBakPath, kRpmTable);
    TEST_ASSERT_TRUE(ConfigLoader::loadAll().signalsOk);
    TEST_ASSERT_EQUAL_STRING("rpm", firstSignalName());
    TEST_ASSERT_TRUE(fileHolds(kConfigPathSignals, kRpmTable));
}

void test_bootPrefersACleanBackupOverADegradedPrimary() {
    stage(kConfigPathSignals, kOneBadSignal);
    stage(kSignalsBakPath, kBoostTable);
    TEST_ASSERT_TRUE(ConfigLoader::loadAll().signalsOk);
    TEST_ASSERT_EQUAL_STRING("boost", firstSignalName());
}

void test_bootKeepsADegradedPrimaryWithoutABackup() {
    stage(kConfigPathSignals, kOneBadSignal);
    TEST_ASSERT_TRUE(ConfigLoader::loadAll().signalsOk);
    TEST_ASSERT_EQUAL_UINT8(1, ConfigLoader::getSignalConfig().signalCount);
}

void test_bootFailsWhenNeitherFileLoads() {
    stage(kConfigPathSignals, kRpmTable);
    TEST_ASSERT_TRUE(ConfigLoader::loadAll().signalsOk);
    stage(kConfigPathSignals, kTruncated);
    TEST_ASSERT_FALSE(ConfigLoader::loadAll().signalsOk);
    TEST_ASSERT_EQUAL_STRING("rpm", firstSignalName());
}

void test_aRejectedUploadKeepsTheLiveFileAndItsBackup() {
    stage(kConfigPathSignals, kRpmTable);
    stage(kSignalsBakPath, kBoostTable);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StorageDriver::ChunkCommit::Rejected),
                            static_cast<uint8_t>(upload(kOneBadSignal)));
    TEST_ASSERT_TRUE(fileHolds(kConfigPathSignals, kRpmTable));
    TEST_ASSERT_TRUE(fileHolds(kSignalsBakPath, kBoostTable));
}

void test_anAcceptedUploadRotatesThePreviousFileToTheBackup() {
    stage(kConfigPathSignals, kRpmTable);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StorageDriver::ChunkCommit::Committed),
                            static_cast<uint8_t>(upload(kBoostTable)));
    TEST_ASSERT_TRUE(fileHolds(kConfigPathSignals, kBoostTable));
    TEST_ASSERT_TRUE(fileHolds(kSignalsBakPath, kRpmTable));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_aCleanTableIsAccepted);
    RUN_TEST(test_brokenJsonIsUnreadable);
    RUN_TEST(test_aMissingSignalListIsRejected);
    RUN_TEST(test_aSignalTheLoaderWouldDropIsRejected);
    RUN_TEST(test_aTablePastTheCapIsRejected);
    RUN_TEST(test_inspectingLeavesTheLiveTableAlone);
    RUN_TEST(test_aCleanPrimaryAlwaysWins);
    RUN_TEST(test_aCleanBackupBeatsAFlawedPrimary);
    RUN_TEST(test_aDegradedTableBeatsNothing);
    RUN_TEST(test_bootFallsBackToTheBackupAndPromotesIt);
    RUN_TEST(test_bootPrefersACleanBackupOverADegradedPrimary);
    RUN_TEST(test_bootKeepsADegradedPrimaryWithoutABackup);
    RUN_TEST(test_bootFailsWhenNeitherFileLoads);
    RUN_TEST(test_aRejectedUploadKeepsTheLiveFileAndItsBackup);
    RUN_TEST(test_anAcceptedUploadRotatesThePreviousFileToTheBackup);
    return UNITY_END();
}
