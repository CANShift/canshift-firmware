#include "hal/usb/bus_scan_line.h"

#include <string.h>
#include <unity.h>

namespace {

constexpr uint32_t kStartMs = 1000;
constexpr uint32_t kProbeMs = 500;
constexpr uint32_t kMaxCount = 0xFFFFFFFFu;
constexpr uint32_t kMaxExtendedId = 0x1FFFFFFFu;

char s_line[BusScanLine::kLineCap];

void probeEveryRate(BusScanRs &scan, const uint32_t (&frames)[kBusScanRateCount], uint32_t errors) {
    uint32_t now = kStartMs;
    for (uint32_t count : frames) {
        for (uint32_t i = 0; i < count; ++i)
            bus_scan_on_frame_rs(&scan, 0x370);
        now += kProbeMs;
        TEST_ASSERT_NOT_EQUAL(kBusScanActionNone, bus_scan_tick_rs(&scan, now, errors));
    }
}

void collect(BusScanRs &scan, const uint32_t *ids, size_t count) {
    for (size_t i = 0; i < count; ++i)
        bus_scan_on_frame_rs(&scan, ids[i]);
    const uint32_t end = scan.phase_started_ms + kBusScanCollectMs;
    TEST_ASSERT_EQUAL(kBusScanActionRestore, bus_scan_tick_rs(&scan, end, 0));
}

size_t render(const BusScanRs &scan) {
    return BusScanLine::format(scan, s_line, sizeof(s_line));
}

}

void setUp() {}
void tearDown() {}

void test_aScanNeverStartedIsIdle() {
    const BusScanRs scan{};
    TEST_ASSERT_GREATER_THAN(0, render(scan));
    TEST_ASSERT_EQUAL_STRING("{\"status\":\"ok\",\"state\":\"idle\"}", s_line);
}

void test_probingReportsTheRateUnderTest() {
    BusScanRs scan{};
    TEST_ASSERT_EQUAL(kBusScanActionInstall, bus_scan_begin_rs(&scan, kStartMs));
    TEST_ASSERT_EQUAL(kBusScanActionInstall, bus_scan_tick_rs(&scan, kStartMs + kProbeMs, 0));
    TEST_ASSERT_GREATER_THAN(0, render(scan));
    TEST_ASSERT_EQUAL_STRING(
        "{\"status\":\"ok\",\"state\":\"probing\",\"kbps\":250,\"step\":2,\"steps\":5}", s_line);
}

void test_collectingIsTheLastStep() {
    BusScanRs scan{};
    TEST_ASSERT_EQUAL(kBusScanActionInstall, bus_scan_begin_rs(&scan, kStartMs));
    probeEveryRate(scan, {600, 0, 0, 0}, 0);
    TEST_ASSERT_GREATER_THAN(0, render(scan));
    TEST_ASSERT_EQUAL_STRING(
        "{\"status\":\"ok\",\"state\":\"collecting\",\"kbps\":500,\"step\":5,\"steps\":5}", s_line);
}

void test_aDetectedBusListsItsProbesAndIds() {
    BusScanRs scan{};
    TEST_ASSERT_EQUAL(kBusScanActionInstall, bus_scan_begin_rs(&scan, kStartMs));
    probeEveryRate(scan, {0, 300, 0, 0}, 0);
    const uint32_t ids[] = {0x370, 0x371, 0x370};
    collect(scan, ids, sizeof(ids) / sizeof(ids[0]));
    TEST_ASSERT_GREATER_THAN(0, render(scan));
    TEST_ASSERT_EQUAL_STRING("{\"status\":\"ok\",\"state\":\"done\",\"outcome\":\"detected\","
                             "\"kbps\":250,\"window_ms\":3000,"
                             "\"probes\":[[500,0,0],[250,300,0],[1000,0,0],[125,0,0]],"
                             "\"ids\":[[880,2],[881,1]],\"truncated\":false}",
                             s_line);
}

void test_aSilentBusReportsNoRate() {
    BusScanRs scan{};
    TEST_ASSERT_EQUAL(kBusScanActionInstall, bus_scan_begin_rs(&scan, kStartMs));
    probeEveryRate(scan, {0, 0, 0, 0}, 0);
    TEST_ASSERT_GREATER_THAN(0, render(scan));
    TEST_ASSERT_NOT_NULL(strstr(s_line, "\"outcome\":\"silent\",\"kbps\":0,"));
    TEST_ASSERT_NOT_NULL(strstr(s_line, "\"ids\":[]"));
}

void test_aFailedScanSaysSo() {
    BusScanRs scan{};
    TEST_ASSERT_EQUAL(kBusScanActionInstall, bus_scan_begin_rs(&scan, kStartMs));
    TEST_ASSERT_EQUAL(kBusScanActionRestore, bus_scan_fail_rs(&scan));
    TEST_ASSERT_GREATER_THAN(0, render(scan));
    TEST_ASSERT_NOT_NULL(strstr(s_line, "\"state\":\"done\",\"outcome\":\"failed\",\"kbps\":0,"));
}

void test_theWorstCaseResultFitsTheLineCap() {
    BusScanRs scan{};
    scan.phase = kBusScanPhaseDone;
    scan.outcome = kBusScanOutcomeDetected;
    scan.install_kbps = 1000;
    scan.truncated = true;
    scan.id_count = kBusScanIdCapacity;
    for (BusScanProbeRs &probe : scan.probes)
        probe = {kMaxCount, kMaxCount, 1000, 0};
    for (BusScanSeenIdRs &seen : scan.ids)
        seen = {kMaxExtendedId, kMaxCount};
    TEST_ASSERT_GREATER_THAN(0, render(scan));
    TEST_ASSERT_NOT_NULL(strstr(s_line, "\"truncated\":true}"));
}

void test_aShortBufferFailsInsteadOfTruncating() {
    const BusScanRs scan{};
    char line[16];
    TEST_ASSERT_EQUAL(0, BusScanLine::format(scan, line, sizeof(line)));
}

void test_aNullBufferIsRefused() {
    const BusScanRs scan{};
    TEST_ASSERT_EQUAL(0, BusScanLine::format(scan, nullptr, 0));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_aScanNeverStartedIsIdle);
    RUN_TEST(test_probingReportsTheRateUnderTest);
    RUN_TEST(test_collectingIsTheLastStep);
    RUN_TEST(test_aDetectedBusListsItsProbesAndIds);
    RUN_TEST(test_aSilentBusReportsNoRate);
    RUN_TEST(test_aFailedScanSaysSo);
    RUN_TEST(test_theWorstCaseResultFitsTheLineCap);
    RUN_TEST(test_aShortBufferFailsInsteadOfTruncating);
    RUN_TEST(test_aNullBufferIsRefused);
    return UNITY_END();
}
