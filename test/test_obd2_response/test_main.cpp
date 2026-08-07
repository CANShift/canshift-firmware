#include "can/obd2_response.h"

#include <stdint.h>
#include <unity.h>

void setUp() {}
void tearDown() {}

void test_parseHeader_validSingleFrame() {
    const uint8_t frame[] = {0x03, 0x41, 0x0c, 0x1a, 0xf8, 0x00, 0x00, 0x00};
    const Obd2Response::Header h = Obd2Response::parseHeader(frame, sizeof(frame));
    TEST_ASSERT_TRUE(h.ok);
    TEST_ASSERT_EQUAL_UINT8(0x03, h.payloadLen);
    TEST_ASSERT_EQUAL_UINT8(0x41, h.mode);
    TEST_ASSERT_EQUAL_UINT8(0x0c, h.pid);
}

void test_parseHeader_rejectsNullAndShort() {
    TEST_ASSERT_FALSE(Obd2Response::parseHeader(nullptr, 8).ok);
    const uint8_t twoBytes[] = {0x02, 0x41};
    TEST_ASSERT_FALSE(Obd2Response::parseHeader(twoBytes, 2).ok);
}

void test_parseHeader_rejectsOutOfRangePayloadLen() {
    const uint8_t tooSmall[] = {0x01, 0x41, 0x0c};
    TEST_ASSERT_FALSE(Obd2Response::parseHeader(tooSmall, 3).ok);
    const uint8_t tooLarge[] = {0x07, 0x41, 0x0c, 0, 0, 0, 0, 0};
    TEST_ASSERT_FALSE(Obd2Response::parseHeader(tooLarge, sizeof(tooLarge)).ok);
}

void test_matches_modeEchoAndPid() {
    const uint8_t frame[] = {0x03, 0x41, 0x0c, 0, 0, 0, 0, 0};
    const Obd2Response::Header h = Obd2Response::parseHeader(frame, sizeof(frame));
    TEST_ASSERT_TRUE(Obd2Response::matches(h, 0x01, 0x0c));
    TEST_ASSERT_FALSE(Obd2Response::matches(h, 0x01, 0x0d));
    TEST_ASSERT_FALSE(Obd2Response::matches(h, 0x09, 0x0c));
}

void test_matches_rejectsInvalidHeader() {
    const Obd2Response::Header bad = {false, 0, 0x41, 0x0c};
    TEST_ASSERT_FALSE(Obd2Response::matches(bad, 0x01, 0x0c));
}

void test_fitsValueBytes_boundary() {
    TEST_ASSERT_TRUE(Obd2Response::fitsValueBytes(3, 2, 8));
    TEST_ASSERT_TRUE(Obd2Response::fitsValueBytes(6, 2, 8));
    TEST_ASSERT_FALSE(Obd2Response::fitsValueBytes(7, 2, 8));
    TEST_ASSERT_FALSE(Obd2Response::fitsValueBytes(0, 9, 8));
}

void test_extractMode03_singleDtc() {
    const uint8_t frame[] = {0x03, 0x43, 0x01, 0x01, 0x33, 0x00, 0x00, 0x00};
    uint8_t out[16] = {0};
    const uint8_t n = Obd2Response::extractMode03Dtcs(frame, sizeof(frame), out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT8(2, n);
    TEST_ASSERT_EQUAL_UINT8(0x01, out[0]);
    TEST_ASSERT_EQUAL_UINT8(0x33, out[1]);
}

void test_extractMode03_twoDtcs() {
    const uint8_t frame[] = {0x05, 0x43, 0x02, 0x01, 0x33, 0x43, 0x01, 0x00};
    uint8_t out[16] = {0};
    const uint8_t n = Obd2Response::extractMode03Dtcs(frame, sizeof(frame), out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT8(4, n);
    const uint8_t expected[] = {0x01, 0x33, 0x43, 0x01};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, 4);
}

void test_extractMode03_zeroCount() {
    const uint8_t frame[] = {0x02, 0x43, 0x00, 0x00, 0x00};
    uint8_t out[16] = {0};
    TEST_ASSERT_EQUAL_UINT8(0, Obd2Response::extractMode03Dtcs(frame, sizeof(frame), out, 16));
}

void test_extractMode03_rejectsWrongServiceId() {
    const uint8_t frame[] = {0x03, 0x41, 0x01, 0x01, 0x33};
    uint8_t out[16] = {0};
    TEST_ASSERT_EQUAL_UINT8(0, Obd2Response::extractMode03Dtcs(frame, sizeof(frame), out, 16));
}

void test_extractMode03_rejectsShortAndNull() {
    uint8_t out[16] = {0};
    const uint8_t twoBytes[] = {0x43, 0x01};
    TEST_ASSERT_EQUAL_UINT8(0, Obd2Response::extractMode03Dtcs(twoBytes, 2, out, 16));
    const uint8_t frame[] = {0x03, 0x43, 0x01, 0x01, 0x33};
    TEST_ASSERT_EQUAL_UINT8(0, Obd2Response::extractMode03Dtcs(nullptr, 5, out, 16));
    TEST_ASSERT_EQUAL_UINT8(0, Obd2Response::extractMode03Dtcs(frame, 5, nullptr, 16));
}

void test_extractMode03_clampsToPresentBytes() {
    // count claims 3 DTCs (6 bytes) but only 5 payload bytes are present —
    // clamp to the 4 that form whole DTC pairs (drops the trailing odd byte).
    const uint8_t frame[] = {0x05, 0x43, 0x03, 0x01, 0x33, 0x43, 0x01, 0x00};
    uint8_t out[16] = {0};
    TEST_ASSERT_EQUAL_UINT8(4, Obd2Response::extractMode03Dtcs(frame, sizeof(frame), out, 16));
}

void test_extractMode03_clampsToOutCap() {
    const uint8_t frame[] = {0x05, 0x43, 0x02, 0x01, 0x33, 0x43, 0x01, 0x00};
    uint8_t out[2] = {0};
    TEST_ASSERT_EQUAL_UINT8(2, Obd2Response::extractMode03Dtcs(frame, sizeof(frame), out, 2));
    TEST_ASSERT_EQUAL_UINT8(0x01, out[0]);
    TEST_ASSERT_EQUAL_UINT8(0x33, out[1]);
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_parseHeader_validSingleFrame);
    RUN_TEST(test_parseHeader_rejectsNullAndShort);
    RUN_TEST(test_parseHeader_rejectsOutOfRangePayloadLen);
    RUN_TEST(test_matches_modeEchoAndPid);
    RUN_TEST(test_matches_rejectsInvalidHeader);
    RUN_TEST(test_fitsValueBytes_boundary);
    RUN_TEST(test_extractMode03_singleDtc);
    RUN_TEST(test_extractMode03_twoDtcs);
    RUN_TEST(test_extractMode03_zeroCount);
    RUN_TEST(test_extractMode03_rejectsWrongServiceId);
    RUN_TEST(test_extractMode03_rejectsShortAndNull);
    RUN_TEST(test_extractMode03_clampsToPresentBytes);
    RUN_TEST(test_extractMode03_clampsToOutCap);
    return UNITY_END();
}
