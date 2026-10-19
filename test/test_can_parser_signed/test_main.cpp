
#include "can/can_parser.h"

#include <unity.h>

namespace {

constexpr float kEpsilon = 1e-4f;

constexpr uint8_t kSigned32NegativeOneLE[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00};

constexpr uint8_t kSigned32NegativeTwoBE[] = {0xFF, 0xFF, 0xFF, 0xFE, 0x00, 0x00, 0x00, 0x00};

constexpr uint8_t kSigned32IntMinBE[] = {0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

constexpr uint8_t kUnsigned24BE[] = {0x12, 0x34, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00};

constexpr uint8_t kBigEndian1234[] = {0x12, 0x34, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
constexpr uint8_t kLittleEndian1234[] = {0x34, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
constexpr float kExpected1234 = 4660.0f;

}

void setUp() {}
void tearDown() {}

void test_decodeBytes_signed32_negativeOne_littleEndian() {
    const float result = CanParser::detail::decodeBytes(
        kSigned32NegativeOneLE, 0, 4, false, true, 0, 1.0f, 0.0f, sizeof(kSigned32NegativeOneLE));
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, -1.0f, result);
}

void test_decodeBytes_signed32_negativeTwo_bigEndian() {
    const float result = CanParser::detail::decodeBytes(kSigned32NegativeTwoBE, 0, 4, true, true, 0,
                                                        1.0f, 0.0f, sizeof(kSigned32NegativeTwoBE));
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, -2.0f, result);
}

void test_decodeBytes_signed32_intMin_bigEndian() {
    const float result = CanParser::detail::decodeBytes(kSigned32IntMinBE, 0, 4, true, true, 0,
                                                        1.0f, 0.0f, sizeof(kSigned32IntMinBE));
    TEST_ASSERT_FLOAT_WITHIN(1.0f, -2147483648.0f, result);
}

void test_decodeBytes_unsigned24_bigEndian() {
    const float result = CanParser::detail::decodeBytes(kUnsigned24BE, 0, 3, true, false, 0, 1.0f,
                                                        0.0f, sizeof(kUnsigned24BE));
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, 1193046.0f, result);
}

void test_decodeBytes_byteLenZero_returnsZero_withoutReadingData() {
    const float result =
        CanParser::detail::decodeBytes(nullptr, 0, 0, false, false, 0, 1.0f, 0.0f, 0);
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, 0.0f, result);
}

void test_decodeBytes_outOfRange_returnsZero() {
    constexpr uint8_t frame[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    const float result =
        CanParser::detail::decodeBytes(frame, 6, 4, false, false, 0, 1.0f, 0.0f, sizeof(frame));
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, 0.0f, result);
}

void test_decodeBytes_endianness_roundTrip() {
    const float beResult = CanParser::detail::decodeBytes(kBigEndian1234, 0, 2, true, false, 0,
                                                          1.0f, 0.0f, sizeof(kBigEndian1234));
    const float leResult = CanParser::detail::decodeBytes(kLittleEndian1234, 0, 2, false, false, 0,
                                                          1.0f, 0.0f, sizeof(kLittleEndian1234));
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, kExpected1234, beResult);
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, kExpected1234, leResult);
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, beResult, leResult);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_decodeBytes_signed32_negativeOne_littleEndian);
    RUN_TEST(test_decodeBytes_signed32_negativeTwo_bigEndian);
    RUN_TEST(test_decodeBytes_signed32_intMin_bigEndian);
    RUN_TEST(test_decodeBytes_unsigned24_bigEndian);
    RUN_TEST(test_decodeBytes_byteLenZero_returnsZero_withoutReadingData);
    RUN_TEST(test_decodeBytes_outOfRange_returnsZero);
    RUN_TEST(test_decodeBytes_endianness_roundTrip);
    return UNITY_END();
}
