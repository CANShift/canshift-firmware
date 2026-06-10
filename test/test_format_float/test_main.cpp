// Parity gate for the Rust port (#1177 R-2).
#include "util/format_float.h"

#include <math.h>
#include <string.h>
#include <unity.h>

void setUp() {}
void tearDown() {}

void test_formatFixed_basicTwoDecimals() {
    char buf[16] = {0};
    const size_t n = FloatFormat::formatFixed(buf, sizeof(buf), 3.14159f, 2);
    TEST_ASSERT_EQUAL_size_t(4, n);
    TEST_ASSERT_EQUAL_STRING("3.14", buf);
}

void test_formatFixed_zeroDecimalsRounds() {
    char buf[16] = {0};
    FloatFormat::formatFixed(buf, sizeof(buf), 3.7f, 0);
    TEST_ASSERT_EQUAL_STRING("4", buf);
}

void test_formatFixed_negativeValue() {
    char buf[16] = {0};
    FloatFormat::formatFixed(buf, sizeof(buf), -12.5f, 1);
    TEST_ASSERT_EQUAL_STRING("-12.5", buf);
}

void test_formatFixed_zeroValueKeepsRequestedPrecision() {
    char buf[16] = {0};
    FloatFormat::formatFixed(buf, sizeof(buf), 0.0f, 3);
    TEST_ASSERT_EQUAL_STRING("0.000", buf);
}

void test_formatFixed_nanLiteralToken() {
    char buf[16] = {0};
    const size_t n = FloatFormat::formatFixed(buf, sizeof(buf), NAN, 2);
    TEST_ASSERT_EQUAL_size_t(3, n);
    TEST_ASSERT_EQUAL_STRING("nan", buf);
}

void test_formatFixed_positiveInfinityToken() {
    char buf[16] = {0};
    FloatFormat::formatFixed(buf, sizeof(buf), INFINITY, 2);
    TEST_ASSERT_EQUAL_STRING("inf", buf);
}

void test_formatFixed_negativeInfinityToken() {
    char buf[16] = {0};
    FloatFormat::formatFixed(buf, sizeof(buf), -INFINITY, 2);
    TEST_ASSERT_EQUAL_STRING("-inf", buf);
}

void test_formatFixed_clampsDecimalsHigh() {
    char buf[24] = {0};
    FloatFormat::formatFixed(buf, sizeof(buf), 1.0f, 99);
    TEST_ASSERT_EQUAL_STRING("1.000000000", buf);
}

void test_formatFixed_clampsDecimalsNegative() {
    char buf[16] = {0};
    FloatFormat::formatFixed(buf, sizeof(buf), 1.5f, -1);
    TEST_ASSERT_EQUAL_STRING("2", buf);
}

void test_formatFixed_truncatesToBufferAndReturnsWouldHave() {
    char buf[4] = {0};
    const size_t n = FloatFormat::formatFixed(buf, sizeof(buf), 123.456f, 2);
    TEST_ASSERT_EQUAL_size_t(6, n);
    TEST_ASSERT_EQUAL_STRING("123", buf);
}

void test_formatFixed_zeroSizeReturnsZero() {
    char dummy = 'X';
    TEST_ASSERT_EQUAL_size_t(0, FloatFormat::formatFixed(&dummy, 0, 1.0f, 2));
    TEST_ASSERT_EQUAL_CHAR('X', dummy);
}

void test_formatFromSpec_noTokenFallsBackToOneDecimal() {
    char buf[16] = {0};
    FloatFormat::formatFromSpec(buf, sizeof(buf), 12.34f, " V");
    TEST_ASSERT_EQUAL_STRING("12.3", buf);
}

void test_formatFromSpec_defaultPrecisionOneWhenNoDot() {
    char buf[16] = {0};
    FloatFormat::formatFromSpec(buf, sizeof(buf), 5.4321f, "%fV");
    TEST_ASSERT_EQUAL_STRING("5.4V", buf);
}

void test_formatFromSpec_explicitPrecision() {
    char buf[16] = {0};
    FloatFormat::formatFromSpec(buf, sizeof(buf), 5.4321f, "%.3fV");
    TEST_ASSERT_EQUAL_STRING("5.432V", buf);
}

void test_formatFromSpec_zeroPrecisionWithSuffix() {
    char buf[16] = {0};
    FloatFormat::formatFromSpec(buf, sizeof(buf), 5.7f, "%.0f bar");
    TEST_ASSERT_EQUAL_STRING("6 bar", buf);
}

void test_formatFromSpec_prefixAndSuffix() {
    char buf[32] = {0};
    FloatFormat::formatFromSpec(buf, sizeof(buf), 3.14f, "value=%.2fkg!");
    TEST_ASSERT_EQUAL_STRING("value=3.14kg!", buf);
}

void test_formatFromSpec_doublePercentIsSkipNotUnescape() {
    char buf[16] = {0};
    // C++ quirk: %% survives in the prefix verbatim, NOT printf-style.
    FloatFormat::formatFromSpec(buf, sizeof(buf), 9.8f, "%%V%.1f");
    TEST_ASSERT_EQUAL_STRING("%%V9.8", buf);
}

void test_formatFromSpec_unreasonablePrecisionBailsAndFallsBack() {
    char buf[16] = {0};
    FloatFormat::formatFromSpec(buf, sizeof(buf), 4.5f, "%.123fV");
    TEST_ASSERT_EQUAL_STRING("4.5", buf);
}

void test_formatFromSpec_nullSpecRendersOneDecimal() {
    char buf[16] = {0};
    FloatFormat::formatFromSpec(buf, sizeof(buf), 1.2345f, nullptr);
    TEST_ASSERT_EQUAL_STRING("1.2", buf);
}

void test_formatFromSpec_emptySpecRendersOneDecimal() {
    char buf[16] = {0};
    FloatFormat::formatFromSpec(buf, sizeof(buf), 1.2345f, "");
    TEST_ASSERT_EQUAL_STRING("1.2", buf);
}

void test_formatGeneral_stripsTrailingZeros() {
    char buf[16] = {0};
    FloatFormat::formatGeneral(buf, sizeof(buf), 1.5f, 4);
    TEST_ASSERT_EQUAL_STRING("1.5", buf);
}

void test_formatGeneral_keepsMeaningfulDecimals() {
    char buf[16] = {0};
    FloatFormat::formatGeneral(buf, sizeof(buf), 1.234f, 4);
    TEST_ASSERT_EQUAL_STRING("1.234", buf);
}

void test_formatGeneral_largeMagnitudeNoDecimals() {
    char buf[16] = {0};
    FloatFormat::formatGeneral(buf, sizeof(buf), 12345.0f, 4);
    TEST_ASSERT_EQUAL_STRING("12345", buf);
}

void test_formatGeneral_subUnitMagnitude() {
    char buf[16] = {0};
    FloatFormat::formatGeneral(buf, sizeof(buf), 0.123f, 4);
    TEST_ASSERT_EQUAL_STRING("0.123", buf);
}

void test_formatGeneral_negativeValue() {
    char buf[16] = {0};
    FloatFormat::formatGeneral(buf, sizeof(buf), -1.5f, 3);
    TEST_ASSERT_EQUAL_STRING("-1.5", buf);
}

void test_formatGeneral_clampsSigDigitsLow() {
    char buf[16] = {0};
    FloatFormat::formatGeneral(buf, sizeof(buf), 9.876f, 0);
    TEST_ASSERT_EQUAL_STRING("10", buf);
}

void test_formatGeneral_nanAndInf() {
    char buf[16] = {0};

    FloatFormat::formatGeneral(buf, sizeof(buf), NAN, 3);
    TEST_ASSERT_EQUAL_STRING("nan", buf);

    FloatFormat::formatGeneral(buf, sizeof(buf), INFINITY, 3);
    TEST_ASSERT_EQUAL_STRING("inf", buf);

    FloatFormat::formatGeneral(buf, sizeof(buf), -INFINITY, 3);
    TEST_ASSERT_EQUAL_STRING("-inf", buf);
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();

    RUN_TEST(test_formatFixed_basicTwoDecimals);
    RUN_TEST(test_formatFixed_zeroDecimalsRounds);
    RUN_TEST(test_formatFixed_negativeValue);
    RUN_TEST(test_formatFixed_zeroValueKeepsRequestedPrecision);
    RUN_TEST(test_formatFixed_nanLiteralToken);
    RUN_TEST(test_formatFixed_positiveInfinityToken);
    RUN_TEST(test_formatFixed_negativeInfinityToken);
    RUN_TEST(test_formatFixed_clampsDecimalsHigh);
    RUN_TEST(test_formatFixed_clampsDecimalsNegative);
    RUN_TEST(test_formatFixed_truncatesToBufferAndReturnsWouldHave);
    RUN_TEST(test_formatFixed_zeroSizeReturnsZero);

    RUN_TEST(test_formatFromSpec_noTokenFallsBackToOneDecimal);
    RUN_TEST(test_formatFromSpec_defaultPrecisionOneWhenNoDot);
    RUN_TEST(test_formatFromSpec_explicitPrecision);
    RUN_TEST(test_formatFromSpec_zeroPrecisionWithSuffix);
    RUN_TEST(test_formatFromSpec_prefixAndSuffix);
    RUN_TEST(test_formatFromSpec_doublePercentIsSkipNotUnescape);
    RUN_TEST(test_formatFromSpec_unreasonablePrecisionBailsAndFallsBack);
    RUN_TEST(test_formatFromSpec_nullSpecRendersOneDecimal);
    RUN_TEST(test_formatFromSpec_emptySpecRendersOneDecimal);

    RUN_TEST(test_formatGeneral_stripsTrailingZeros);
    RUN_TEST(test_formatGeneral_keepsMeaningfulDecimals);
    RUN_TEST(test_formatGeneral_largeMagnitudeNoDecimals);
    RUN_TEST(test_formatGeneral_subUnitMagnitude);
    RUN_TEST(test_formatGeneral_negativeValue);
    RUN_TEST(test_formatGeneral_clampsSigDigitsLow);
    RUN_TEST(test_formatGeneral_nanAndInf);

    return UNITY_END();
}
