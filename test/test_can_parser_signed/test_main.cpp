// test_main.cpp — Coverage extensions for CanParser::detail::decodeBytes (#912).
//
// The base suite in test_can_parser/ covers the three "happy-path" modes
// (LE unsigned, BE signed 2-byte, bit-mask). These tests close the gaps
// flagged in the audit:
//   - 4-byte signed negative (sign-extension path that uses the int32_t cast
//     directly because the byteLen<4 mask would shift by 32, which is UB).
//   - 3-byte big-endian unsigned (the 24-bit BE walk has no other coverage).
//   - byteLen == 0 boundary (must return 0 without touching `data`).
//   - LE/BE round-trip on the same raw value (locks endianness handling).
//   - Out-of-range start+byteLen rejection.

#include "can/can_parser.h"

#include <unity.h>

namespace {

constexpr float kEpsilon = 1e-4f;

// 4-byte little-endian payload representing the signed int32_t value -1
// (0xFFFFFFFF). For byteLen == 4 the production code skips the manual sign
// extension and relies on the `static_cast<int32_t>(raw)` reinterpret to keep
// the two's-complement pattern intact — this anchors that contract.
constexpr uint8_t kSigned32NegativeOneLE[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00};

// 4-byte big-endian payload for int32_t == -2 (0xFFFFFFFE).
constexpr uint8_t kSigned32NegativeTwoBE[] = {0xFF, 0xFF, 0xFF, 0xFE, 0x00, 0x00, 0x00, 0x00};

// 4-byte big-endian payload for INT32_MIN (0x80000000). The high bit is set —
// catches a regression where decodeBytes accidentally treats the value as
// unsigned because the byteLen==4 branch skips the explicit sign-extend.
constexpr uint8_t kSigned32IntMinBE[] = {0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// 3-byte big-endian unsigned payload: 0x123456 == 1193046.
constexpr uint8_t kUnsigned24BE[] = {0x12, 0x34, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00};

// Same raw value (0x1234) encoded twice — once big-endian, once little-endian.
// The decoder must yield identical floats when the endianness flag matches the
// byte order, regardless of byte slot offsets.
constexpr uint8_t kBigEndian1234[] = {0x12, 0x34, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
constexpr uint8_t kLittleEndian1234[] = {0x34, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
constexpr float kExpected1234 = 4660.0f; // 0x1234

} // namespace

void setUp() {}
void tearDown() {}

void test_decodeBytes_signed32_negativeOne_littleEndian() {
    const float result =
        CanParser::detail::decodeBytes(kSigned32NegativeOneLE,
                                       /*startByte=*/0, /*byteLen=*/4,
                                       /*bigEndian=*/false, /*isSigned=*/true, /*bitMask=*/0,
                                       /*scale=*/1.0f, /*offset=*/0.0f);
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, -1.0f, result);
}

void test_decodeBytes_signed32_negativeTwo_bigEndian() {
    const float result =
        CanParser::detail::decodeBytes(kSigned32NegativeTwoBE,
                                       /*startByte=*/0, /*byteLen=*/4,
                                       /*bigEndian=*/true, /*isSigned=*/true, /*bitMask=*/0,
                                       /*scale=*/1.0f, /*offset=*/0.0f);
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, -2.0f, result);
}

void test_decodeBytes_signed32_intMin_bigEndian() {
    const float result =
        CanParser::detail::decodeBytes(kSigned32IntMinBE,
                                       /*startByte=*/0, /*byteLen=*/4,
                                       /*bigEndian=*/true, /*isSigned=*/true, /*bitMask=*/0,
                                       /*scale=*/1.0f, /*offset=*/0.0f);
    // INT32_MIN == -2147483648 — float can represent it exactly.
    TEST_ASSERT_FLOAT_WITHIN(1.0f, -2147483648.0f, result);
}

void test_decodeBytes_unsigned24_bigEndian() {
    const float result =
        CanParser::detail::decodeBytes(kUnsigned24BE,
                                       /*startByte=*/0, /*byteLen=*/3,
                                       /*bigEndian=*/true, /*isSigned=*/false, /*bitMask=*/0,
                                       /*scale=*/1.0f, /*offset=*/0.0f);
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, 1193046.0f, result);
}

// byteLen == 0 is an invalid signal definition. The production code short-
// circuits to 0.0f without dereferencing `data`. Passing nullptr documents
// that contract — if a future refactor reads `data` before the check, ASan
// in the native env will catch it.
void test_decodeBytes_byteLenZero_returnsZero_withoutReadingData() {
    const float result = CanParser::detail::decodeBytes(
        /*data=*/nullptr,
        /*startByte=*/0, /*byteLen=*/0,
        /*bigEndian=*/false, /*isSigned=*/false, /*bitMask=*/0,
        /*scale=*/1.0f, /*offset=*/0.0f);
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, 0.0f, result);
}

// start + byteLen > 8 must return 0 without reading past the frame. ASan in
// the native env catches a regression that misses the bound and walks off
// the array.
void test_decodeBytes_outOfRange_returnsZero() {
    constexpr uint8_t frame[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    const float result =
        CanParser::detail::decodeBytes(frame,
                                       /*startByte=*/6, /*byteLen=*/4,
                                       /*bigEndian=*/false, /*isSigned=*/false, /*bitMask=*/0,
                                       /*scale=*/1.0f, /*offset=*/0.0f);
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, 0.0f, result);
}

// Round-trip: 0x1234 encoded big- vs little-endian must decode to the same
// scalar value. Anchors the BE/LE swap path so a regression that reverses the
// loop direction surfaces immediately.
void test_decodeBytes_endianness_roundTrip() {
    const float beResult =
        CanParser::detail::decodeBytes(kBigEndian1234,
                                       /*startByte=*/0, /*byteLen=*/2,
                                       /*bigEndian=*/true, /*isSigned=*/false, /*bitMask=*/0,
                                       /*scale=*/1.0f, /*offset=*/0.0f);
    const float leResult =
        CanParser::detail::decodeBytes(kLittleEndian1234,
                                       /*startByte=*/0, /*byteLen=*/2,
                                       /*bigEndian=*/false, /*isSigned=*/false, /*bitMask=*/0,
                                       /*scale=*/1.0f, /*offset=*/0.0f);
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, kExpected1234, beResult);
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, kExpected1234, leResult);
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, beResult, leResult);
}

int main(int /*argc*/, char ** /*argv*/) {
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
