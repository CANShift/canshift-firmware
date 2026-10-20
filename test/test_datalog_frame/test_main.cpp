#include "datalog_rs.h"

#include <cmath>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unity.h>

void setUp() {}
void tearDown() {}

namespace {

constexpr int kRpmSlot = 0;
constexpr int kThrottleSlot = 1;
constexpr int kGearSlot = 12;
constexpr uint32_t kRingCapacity = 32;

void canonicalValues(float *values) {
    for (int i = 0; i < kDatalogSignalSlots; i++)
        values[i] = 0.0f;
    values[kRpmSlot] = 3500.0f;
    values[kThrottleSlot] = 45.5f;
    values[kGearSlot] = -1.0f;
}

uint64_t canonicalMask() {
    return (1ULL << kRpmSlot) | (1ULL << kThrottleSlot) | (1ULL << kGearSlot);
}

}

void test_preamble_isMagicThenVersion() {
    uint8_t buf[kDatalogPreambleBytes] = {0};
    TEST_ASSERT_EQUAL_size_t(kDatalogPreambleBytes, datalog_preamble_rs(buf, sizeof(buf)));
    const uint8_t expected[] = {'C', 'S', 'L', 'G', 0x01};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, buf, sizeof(expected));
}

void test_sample_canonicalVector_matchesCore() {
    float values[kDatalogSignalSlots];
    canonicalValues(values);
    uint8_t buf[kDatalogMaxSampleBytes] = {0};
    const size_t len = datalog_sample_rs(buf, sizeof(buf), 1000, canonicalMask(), values);
    const uint8_t expected[] = {0x02, 0x18, 0x00, 0xe8, 0x03, 0x00, 0x00, 0x03, 0x10, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x5a, 0x45, 0x00,
                                0x00, 0x36, 0x42, 0x00, 0x00, 0x80, 0xbf, 0xd8, 0xf1};
    TEST_ASSERT_EQUAL_size_t(sizeof(expected), len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, buf, sizeof(expected));
}

void test_markers_canonicalVectors_matchCore() {
    uint8_t buf[kDatalogMaxMarkerBytes] = {0};
    const uint8_t lap[] = {0x03, 0x0a, 0x00, 0x48, 0xee, 0x00, 0x00, 0x01,
                           0x00, 0x54, 0xec, 0x00, 0x00, 0x60, 0xd9};
    TEST_ASSERT_EQUAL_size_t(sizeof(lap), datalog_lap_rs(buf, sizeof(buf), 61000, 1, 60500));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(lap, buf, sizeof(lap));
    const uint8_t end[] = {0x04, 0x08, 0x00, 0x30, 0xf2, 0x00, 0x00,
                           0x03, 0x00, 0x00, 0x00, 0x78, 0x5d};
    TEST_ASSERT_EQUAL_size_t(sizeof(end), datalog_end_rs(buf, sizeof(buf), 62000, 3));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(end, buf, sizeof(end));
}

void test_sample_skipsNonFinite() {
    float values[kDatalogSignalSlots];
    canonicalValues(values);
    values[kThrottleSlot] = std::nanf("");
    uint8_t buf[kDatalogMaxSampleBytes] = {0};
    TEST_ASSERT_EQUAL_size_t(25, datalog_sample_rs(buf, sizeof(buf), 0, canonicalMask(), values));
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[7]);
}

void test_header_fitsItsBound() {
    static uint8_t buf[kDatalogMaxHeaderBytes];
    const size_t len = datalog_header_rs(buf, sizeof(buf), 50, 7);
    TEST_ASSERT_GREATER_THAN_size_t(0, len);
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(50, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(7, buf[5]);
}

void test_encoders_rejectNullAndShortBuffers() {
    float values[kDatalogSignalSlots];
    canonicalValues(values);
    uint8_t tiny[4] = {0};
    TEST_ASSERT_EQUAL_size_t(0, datalog_sample_rs(nullptr, 0, 0, canonicalMask(), values));
    TEST_ASSERT_EQUAL_size_t(0, datalog_sample_rs(tiny, sizeof(tiny), 0, 1, nullptr));
    TEST_ASSERT_EQUAL_size_t(0, datalog_lap_rs(tiny, sizeof(tiny), 0, 0, 0));
}

void test_ring_dropsWholeRecordsAndCountsThem() {
    static uint8_t storage[kRingCapacity];
    DatalogRingRs ring;
    TEST_ASSERT_TRUE(datalog_ring_init_rs(&ring, storage, kRingCapacity));
    const uint8_t record[20] = {1};
    TEST_ASSERT_TRUE(datalog_ring_push_rs(&ring, record, sizeof(record)));
    TEST_ASSERT_FALSE(datalog_ring_push_rs(&ring, record, sizeof(record)));
    TEST_ASSERT_EQUAL_UINT32(1, datalog_ring_dropped_rs(&ring));
    uint8_t out[kRingCapacity] = {0};
    TEST_ASSERT_EQUAL_size_t(sizeof(record), datalog_ring_pop_rs(&ring, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(record, out, sizeof(record));
    TEST_ASSERT_EQUAL_UINT32(0, datalog_ring_used_rs(&ring));
}

void test_ring_refusesBadStorage() {
    static uint8_t storage[24];
    DatalogRingRs ring;
    TEST_ASSERT_FALSE(datalog_ring_init_rs(&ring, storage, sizeof(storage)));
    TEST_ASSERT_FALSE(datalog_ring_init_rs(&ring, nullptr, kRingCapacity));
    TEST_ASSERT_FALSE(datalog_ring_init_rs(nullptr, storage, 16));
}

void test_session_opensOnALiveBusAndClosesInWorkshop() {
    DatalogSessionRs session = {};
    DatalogSessionInputsRs live = {false, true, false, 0, 0};
    TEST_ASSERT_EQUAL_UINT8(kDatalogActionOpen, datalog_session_step_rs(&session, &live));
    DatalogSessionInputsRs workshop = live;
    workshop.workshop = true;
    TEST_ASSERT_EQUAL_UINT8(kDatalogActionClose, datalog_session_step_rs(&session, &workshop));
    TEST_ASSERT_EQUAL_UINT8(kDatalogActionNone, datalog_session_step_rs(nullptr, &live));
}

void test_session_haltedWaitsForSilence() {
    DatalogSessionRs session = {};
    DatalogSessionInputsRs live = {false, true, false, 0, 0};
    TEST_ASSERT_EQUAL_UINT8(kDatalogActionOpen, datalog_session_step_rs(&session, &live));
    datalog_session_halt_rs(&session);
    TEST_ASSERT_EQUAL_UINT8(kDatalogActionNone, datalog_session_step_rs(&session, &live));
    DatalogSessionInputsRs silent = {true, true, false, 0, 1};
    TEST_ASSERT_EQUAL_UINT8(kDatalogActionNone, datalog_session_step_rs(&session, &silent));
    TEST_ASSERT_EQUAL_UINT8(kDatalogActionOpen, datalog_session_step_rs(&session, &live));
}

void test_lapWatch_reportsEachNewLapOnce() {
    DatalogLapWatchRs watch = {};
    datalog_lap_rebase_rs(&watch, 3, 1);
    TEST_ASSERT_FALSE(datalog_lap_observe_rs(&watch, 3, 1));
    TEST_ASSERT_TRUE(datalog_lap_observe_rs(&watch, 4, 1));
    TEST_ASSERT_FALSE(datalog_lap_observe_rs(&watch, 0, 2));
}

void test_logNames_roundTrip() {
    uint8_t name[kDatalogNameBytes] = {0};
    TEST_ASSERT_EQUAL_size_t(kDatalogNameBytes, datalog_log_name_rs(7, name, sizeof(name)));
    TEST_ASSERT_EQUAL_MEMORY("S00007.csl", name, kDatalogNameBytes);
    TEST_ASSERT_TRUE(datalog_is_log_name_rs(name, sizeof(name)));
    const char traversal[] = "../S00007.csl";
    TEST_ASSERT_FALSE(datalog_is_log_name_rs(reinterpret_cast<const uint8_t *>(traversal),
                                             sizeof(traversal) - 1));
    TEST_ASSERT_FALSE(datalog_is_log_name_rs(nullptr, 0));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_preamble_isMagicThenVersion);
    RUN_TEST(test_sample_canonicalVector_matchesCore);
    RUN_TEST(test_markers_canonicalVectors_matchCore);
    RUN_TEST(test_sample_skipsNonFinite);
    RUN_TEST(test_header_fitsItsBound);
    RUN_TEST(test_encoders_rejectNullAndShortBuffers);
    RUN_TEST(test_ring_dropsWholeRecordsAndCountsThem);
    RUN_TEST(test_ring_refusesBadStorage);
    RUN_TEST(test_session_opensOnALiveBusAndClosesInWorkshop);
    RUN_TEST(test_session_haltedWaitsForSilence);
    RUN_TEST(test_lapWatch_reportsEachNewLapOnce);
    RUN_TEST(test_logNames_roundTrip);
    return UNITY_END();
}
