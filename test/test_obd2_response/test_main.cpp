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

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_parseHeader_validSingleFrame);
    RUN_TEST(test_parseHeader_rejectsNullAndShort);
    RUN_TEST(test_parseHeader_rejectsOutOfRangePayloadLen);
    RUN_TEST(test_matches_modeEchoAndPid);
    RUN_TEST(test_matches_rejectsInvalidHeader);
    RUN_TEST(test_fitsValueBytes_boundary);
    return UNITY_END();
}
