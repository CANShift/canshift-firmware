#include "can/can_parser.h"

#include <unity.h>

namespace {

constexpr uint8_t kLittleEndianFrame[] = {0x34, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
constexpr float kLittleEndianScale = 0.1f;
constexpr float kLittleEndianExpected = 466.0f;

constexpr uint8_t kBigEndianSignedFrame[] = {0xFF, 0xEC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
constexpr float kBigEndianSignedExpected = -20.0f;

constexpr uint8_t kFlagsFrame[] = {0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
constexpr uint8_t kMaskBit6 = 0x40;
constexpr uint8_t kMaskBit0 = 0x01;

constexpr float kEpsilon = 1e-4f;

} // namespace

void setUp() {}
void tearDown() {}

void test_decodeBytes_littleEndian_unsigned_scaleOffset() {
    const float result = CanParser::detail::decodeBytes(
        kLittleEndianFrame,
        /*startByte=*/0, /*byteLen=*/2,
        /*bigEndian=*/false, /*isSigned=*/false, /*bitMask=*/0,
        kLittleEndianScale, /*offset=*/0.0f);
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, kLittleEndianExpected, result);
}

void test_decodeBytes_bigEndian_signed_negative() {
    const float result = CanParser::detail::decodeBytes(
        kBigEndianSignedFrame,
        /*startByte=*/0, /*byteLen=*/2,
        /*bigEndian=*/true, /*isSigned=*/true, /*bitMask=*/0,
        /*scale=*/1.0f, /*offset=*/0.0f);
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, kBigEndianSignedExpected, result);
}

void test_decodeBytes_bitMask_extractsFlag() {
    const float setResult = CanParser::detail::decodeBytes(
        kFlagsFrame,
        /*startByte=*/0, /*byteLen=*/1,
        /*bigEndian=*/false, /*isSigned=*/false, kMaskBit6,
        /*scale=*/1.0f, /*offset=*/0.0f);
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, 1.0f, setResult);

    const float clearResult = CanParser::detail::decodeBytes(
        kFlagsFrame,
        /*startByte=*/0, /*byteLen=*/1,
        /*bigEndian=*/false, /*isSigned=*/false, kMaskBit0,
        /*scale=*/1.0f, /*offset=*/0.0f);
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, 0.0f, clearResult);
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_decodeBytes_littleEndian_unsigned_scaleOffset);
    RUN_TEST(test_decodeBytes_bigEndian_signed_negative);
    RUN_TEST(test_decodeBytes_bitMask_extractsFlag);
    return UNITY_END();
}
