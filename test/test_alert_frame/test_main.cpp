#include "hal/ble/alert_frame.h"

#include <cmath>
#include <stddef.h>
#include <stdint.h>
#include <unity.h>

void setUp() {}
void tearDown() {}

namespace {

constexpr uint32_t kSecondMs = 1000;

AlertFrame::Entry oilPressureEntry() {
    return {0, 0.4f, true, 1.0f, true, false, true, 12, "oil_press_bar"};
}

AlertFrame::Entry secondConfiguredEgtEntry() {
    return {8, 951.5f, true, 950.0f, false, false, true, 3, "egt_c"};
}

AlertFrame::Entry acknowledgedCoolantEntry() {
    return {1, 112.0f, true, 110.0f, false, true, false, 95, "coolant_temp_c"};
}

} // namespace

void test_encode_noAlert_versionAndZeroCount() {
    uint8_t buf[AlertFrame::kMaxFrameBytes] = {0};
    const size_t len = AlertFrame::encode(nullptr, 0, buf, sizeof(buf));
    const uint8_t expected[] = {0x02, 0x00};
    TEST_ASSERT_EQUAL_size_t(sizeof(expected), len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, buf, sizeof(expected));
}

void test_encode_oilPressureVector_matchesCore() {
    const AlertFrame::Entry entries[] = {oilPressureEntry()};
    uint8_t buf[AlertFrame::kMaxFrameBytes] = {0};
    const size_t len = AlertFrame::encode(entries, 1, buf, sizeof(buf));

    const uint8_t expected[] = {0x02, 0x01, 0x00, 0x0b, 0x90, 0x01, 0x00, 0x00, 0xe8, 0x03,
                                0x00, 0x00, 0x0c, 0x00, 0x0d, 0x6f, 0x69, 0x6c, 0x5f, 0x70,
                                0x72, 0x65, 0x73, 0x73, 0x5f, 0x62, 0x61, 0x72};
    TEST_ASSERT_EQUAL_size_t(sizeof(expected), len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, buf, sizeof(expected));
}

void test_encode_configuredAndCoolantVector_matchesCore() {
    const AlertFrame::Entry entries[] = {secondConfiguredEgtEntry(), acknowledgedCoolantEntry()};
    uint8_t buf[AlertFrame::kMaxFrameBytes] = {0};
    const size_t len = AlertFrame::encode(entries, 2, buf, sizeof(buf));

    const uint8_t expected[] = {0x02, 0x02, 0x08, 0x09, 0xcc, 0x84, 0x0e, 0x00, 0xf0, 0x7e,
                                0x0e, 0x00, 0x03, 0x00, 0x05, 0x65, 0x67, 0x74, 0x5f, 0x63,
                                0x01, 0x05, 0x80, 0xb5, 0x01, 0x00, 0xb0, 0xad, 0x01, 0x00,
                                0x5f, 0x00, 0x0e, 0x63, 0x6f, 0x6f, 0x6c, 0x61, 0x6e, 0x74,
                                0x5f, 0x74, 0x65, 0x6d, 0x70, 0x5f, 0x63};
    TEST_ASSERT_EQUAL_size_t(sizeof(expected), len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, buf, sizeof(expected));
}

void test_encode_unknownLimit_clearsLimitBitsAndZeroesLimit() {
    AlertFrame::Entry entry = oilPressureEntry();
    entry.hasLimit = false;
    uint8_t buf[AlertFrame::kMaxFrameBytes] = {0};
    TEST_ASSERT_EQUAL_size_t(28, AlertFrame::encode(&entry, 1, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_HEX8(0x08, buf[3]);
    const uint8_t zero[] = {0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(zero, buf + 8, sizeof(zero));
}

void test_encode_saturatesActiveSeconds() {
    AlertFrame::Entry entry = oilPressureEntry();
    entry.activeSeconds = 100000;
    uint8_t buf[AlertFrame::kMaxFrameBytes] = {0};
    TEST_ASSERT_EQUAL_size_t(28, AlertFrame::encode(&entry, 1, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_HEX8(0xff, buf[12]);
    TEST_ASSERT_EQUAL_HEX8(0xff, buf[13]);
}

void test_encode_dropsEntriesThatDoNotFit_keepsTheFirst() {
    const AlertFrame::Entry entries[] = {oilPressureEntry(), acknowledgedCoolantEntry()};
    uint8_t buf[30] = {0};
    TEST_ASSERT_EQUAL_size_t(28, AlertFrame::encode(entries, 2, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_UINT8(1, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0, buf[2]);
}

void test_encode_skipsUnencodableEntries() {
    AlertFrame::Entry nan = oilPressureEntry();
    nan.value = std::nanf("");
    AlertFrame::Entry unnamed = acknowledgedCoolantEntry();
    unnamed.signal = "";
    AlertFrame::Entry unknownSource = secondConfiguredEgtEntry();
    unknownSource.source = AlertFrame::kSourceCount;
    const AlertFrame::Entry entries[] = {nan, unnamed, unknownSource};
    uint8_t buf[AlertFrame::kMaxFrameBytes] = {0};
    TEST_ASSERT_EQUAL_size_t(2, AlertFrame::encode(entries, 3, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_UINT8(0, buf[1]);
}

void test_encode_rejectsNullBufferAndTooSmallHeader() {
    uint8_t buf[1] = {0};
    TEST_ASSERT_EQUAL_size_t(0, AlertFrame::encode(nullptr, 0, nullptr, 8));
    TEST_ASSERT_EQUAL_size_t(0, AlertFrame::encode(nullptr, 0, buf, sizeof(buf)));
}

void test_onsets_countWholeSecondsSinceTheSourceTurnedCritical() {
    AlertFrame::Onsets onsets = {};
    AlertFrame::trackOnsets(onsets, 1u << 2, 5000);
    AlertFrame::trackOnsets(onsets, 1u << 2, 5000 + 3 * kSecondMs + 999);
    TEST_ASSERT_EQUAL_UINT32(3, AlertFrame::activeSeconds(onsets, 2, 5000 + 3 * kSecondMs + 999));
}

void test_onsets_restartWhenTheSourceClearsAndReturns() {
    AlertFrame::Onsets onsets = {};
    AlertFrame::trackOnsets(onsets, 1u << 0, 1000);
    AlertFrame::trackOnsets(onsets, 0, 9000);
    AlertFrame::trackOnsets(onsets, 1u << 0, 20000);
    TEST_ASSERT_EQUAL_UINT32(1, AlertFrame::activeSeconds(onsets, 0, 21000));
}

void test_cadence_sendsFirstThenOnlyOnChangeOrKeepalive() {
    AlertFrame::Cadence cadence = {};
    TEST_ASSERT_TRUE(AlertFrame::shouldSend(cadence, 4));
    for (uint16_t i = 1; i < AlertFrame::kKeepaliveTicks; i++)
        TEST_ASSERT_FALSE(AlertFrame::shouldSend(cadence, 4));
    TEST_ASSERT_TRUE(AlertFrame::shouldSend(cadence, 4));
    TEST_ASSERT_TRUE(AlertFrame::shouldSend(cadence, 5));
}

void test_cadence_resetForcesResend() {
    AlertFrame::Cadence cadence = {};
    TEST_ASSERT_TRUE(AlertFrame::shouldSend(cadence, 0));
    TEST_ASSERT_FALSE(AlertFrame::shouldSend(cadence, 0));
    AlertFrame::resetCadence(cadence);
    TEST_ASSERT_TRUE(AlertFrame::shouldSend(cadence, 0));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_encode_noAlert_versionAndZeroCount);
    RUN_TEST(test_encode_oilPressureVector_matchesCore);
    RUN_TEST(test_encode_configuredAndCoolantVector_matchesCore);
    RUN_TEST(test_encode_unknownLimit_clearsLimitBitsAndZeroesLimit);
    RUN_TEST(test_encode_saturatesActiveSeconds);
    RUN_TEST(test_encode_dropsEntriesThatDoNotFit_keepsTheFirst);
    RUN_TEST(test_encode_skipsUnencodableEntries);
    RUN_TEST(test_encode_rejectsNullBufferAndTooSmallHeader);
    RUN_TEST(test_onsets_countWholeSecondsSinceTheSourceTurnedCritical);
    RUN_TEST(test_onsets_restartWhenTheSourceClearsAndReturns);
    RUN_TEST(test_cadence_sendsFirstThenOnlyOnChangeOrKeepalive);
    RUN_TEST(test_cadence_resetForcesResend);
    return UNITY_END();
}
