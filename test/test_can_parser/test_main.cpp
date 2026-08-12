#include "can/can_parser.h"
#include "can_parser_rs.h"

#include <unity.h>
#include <cmath>

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

void test_evalTokensRefs_resolvesReference() {
    FfiTok tokens[CANSHIFT_EXPR_MAX_TOKENS];
    const char expr[] = "ID37+1";
    const int32_t n = lex_expr_rs(reinterpret_cast<const uint8_t *>(expr), sizeof(expr) - 1, tokens,
                                  CANSHIFT_EXPR_MAX_TOKENS);
    TEST_ASSERT_GREATER_THAN_INT32(0, n);

    const RefValueRs refs[] = {{37, 41.0f}};
    const float ok =
        eval_tokens_refs_rs(kLittleEndianFrame, 0, 2, false, false, 0, 1.0f, 0.0f,
                            sizeof(kLittleEndianFrame), tokens, static_cast<size_t>(n), refs, 1);
    TEST_ASSERT_EQUAL_FLOAT(42.0f, ok);

    const float missing =
        eval_tokens_refs_rs(kLittleEndianFrame, 0, 2, false, false, 0, 1.0f, 0.0f,
                            sizeof(kLittleEndianFrame), tokens, static_cast<size_t>(n), nullptr, 0);
    TEST_ASSERT_TRUE(isnan(missing));
}

void test_evalTokensRefs_missingRefDoesNotReadAsZero() {
    FfiTok tokens[CANSHIFT_EXPR_MAX_TOKENS];
    const char expr[] = "ID481|ID482";
    const int32_t n = lex_expr_rs(reinterpret_cast<const uint8_t *>(expr), sizeof(expr) - 1, tokens,
                                  CANSHIFT_EXPR_MAX_TOKENS);
    TEST_ASSERT_GREATER_THAN_INT32(0, n);

    const RefValueRs onlyOne[] = {{481, 1.0f}};
    const float partial =
        eval_tokens_refs_rs(kFlagsFrame, 0, 1, false, false, 0, 1.0f, 0.0f, sizeof(kFlagsFrame),
                            tokens, static_cast<size_t>(n), onlyOne, 1);
    TEST_ASSERT_TRUE(isnan(partial));
}

void setUp() {}
void tearDown() {}

void test_decodeBytes_littleEndian_unsigned_scaleOffset() {
    const float result = CanParser::detail::decodeBytes(
        kLittleEndianFrame,
        /*startByte=*/0, /*byteLen=*/2,
        /*bigEndian=*/false, /*isSigned=*/false, /*bitMask=*/0, kLittleEndianScale, /*offset=*/0.0f,
        /*dataLen=*/sizeof(kLittleEndianFrame));
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, kLittleEndianExpected, result);
}

void test_decodeBytes_bigEndian_signed_negative() {
    const float result = CanParser::detail::decodeBytes(
        kBigEndianSignedFrame,
        /*startByte=*/0, /*byteLen=*/2,
        /*bigEndian=*/true, /*isSigned=*/true, /*bitMask=*/0,
        /*scale=*/1.0f, /*offset=*/0.0f, /*dataLen=*/sizeof(kBigEndianSignedFrame));
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, kBigEndianSignedExpected, result);
}

void test_decodeBytes_bitMask_extractsFlag() {
    const float setResult = CanParser::detail::decodeBytes(
        kFlagsFrame,
        /*startByte=*/0, /*byteLen=*/1,
        /*bigEndian=*/false, /*isSigned=*/false, kMaskBit6,
        /*scale=*/1.0f, /*offset=*/0.0f, /*dataLen=*/sizeof(kFlagsFrame));
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, 1.0f, setResult);

    const float clearResult = CanParser::detail::decodeBytes(
        kFlagsFrame,
        /*startByte=*/0, /*byteLen=*/1,
        /*bigEndian=*/false, /*isSigned=*/false, kMaskBit0,
        /*scale=*/1.0f, /*offset=*/0.0f, /*dataLen=*/sizeof(kFlagsFrame));
    TEST_ASSERT_FLOAT_WITHIN(kEpsilon, 0.0f, clearResult);
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_decodeBytes_littleEndian_unsigned_scaleOffset);
    RUN_TEST(test_decodeBytes_bigEndian_signed_negative);
    RUN_TEST(test_decodeBytes_bitMask_extractsFlag);
    RUN_TEST(test_evalTokensRefs_resolvesReference);
    RUN_TEST(test_evalTokensRefs_missingRefDoesNotReadAsZero);
    return UNITY_END();
}
