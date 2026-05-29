// test_main.cpp — Unity parity tests for FloatFormat::{formatFixed,
// formatFromSpec, formatGeneral} (#1177 R-2).
//
// Locks the observable contract of the three float formatters used across
// the firmware (top bar, widget helpers, BLE server, USB telemetry). Until
// this suite landed, the C++ surface had no direct unit coverage — issues
// like #305 / #405 hit it indirectly through display regressions. The
// Rust port (#1177 R-2) reuses these tests as its parity gate: anything
// observable here must hold for both the C++ and Rust impls.

#include "util/format_float.h"

#include <math.h>
#include <string.h>
#include <unity.h>

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------------------
// formatFixed
// ---------------------------------------------------------------------------

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
    // 99 decimals requested → clamped to 9.
    FloatFormat::formatFixed(buf, sizeof(buf), 1.0f, 99);
    TEST_ASSERT_EQUAL_STRING("1.000000000", buf);
}

void test_formatFixed_clampsDecimalsNegative() {
    char buf[16] = {0};
    // Negative clamps to 0; 0.5f + 0.5 truncation → 2 for 1.5.
    FloatFormat::formatFixed(buf, sizeof(buf), 1.5f, -1);
    TEST_ASSERT_EQUAL_STRING("2", buf);
}

void test_formatFixed_truncatesToBufferAndReturnsWouldHave() {
    char buf[4] = {0}; // 3 visible chars + NUL
    const size_t n = FloatFormat::formatFixed(buf, sizeof(buf), 123.456f, 2);
    // Would-have-written length is 6 ("123.46"). Buffer holds first 3 + NUL.
    TEST_ASSERT_EQUAL_size_t(6, n);
    TEST_ASSERT_EQUAL_STRING("123", buf);
}

void test_formatFixed_zeroSizeReturnsZero() {
    char dummy = 'X';
    TEST_ASSERT_EQUAL_size_t(0, FloatFormat::formatFixed(&dummy, 0, 1.0f, 2));
    TEST_ASSERT_EQUAL_CHAR('X', dummy); // untouched
}

// ---------------------------------------------------------------------------
// formatFromSpec
// ---------------------------------------------------------------------------

void test_formatFromSpec_noTokenFallsBackToOneDecimal() {
    char buf[16] = {0};
    // " V" has no `%f` — render as `%.1f`, IGNORE the spec.
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
    // `%%V%.1f` — the scanner skips `%%` so the second `%` isn't parsed as a
    // conversion, but the prefix `spec[0..percent]` is copied verbatim so the
    // `%%` survives as-is in the output. Quirk vs printf — documented.
    FloatFormat::formatFromSpec(buf, sizeof(buf), 9.8f, "%%V%.1f");
    TEST_ASSERT_EQUAL_STRING("%%V9.8", buf);
}

void test_formatFromSpec_unreasonablePrecisionBailsAndFallsBack() {
    char buf[16] = {0};
    // `.123` is > 2 digits → bail, then no `f` after → fall through to no-
    // token branch → render `%.1f` of value, IGNORING the entire spec.
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

// ---------------------------------------------------------------------------
// formatGeneral
// ---------------------------------------------------------------------------

void test_formatGeneral_stripsTrailingZeros() {
    char buf[16] = {0};
    FloatFormat::formatGeneral(buf, sizeof(buf), 1.5f, 4);
    // 4 sig × 1 int → 3 decimals → "1.500" → strip → "1.5".
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
    // 4 sig × 5 int → 0 decimals → "12345" (no strip).
    TEST_ASSERT_EQUAL_STRING("12345", buf);
}

void test_formatGeneral_subUnitMagnitude() {
    char buf[16] = {0};
    // abs < 1 → intDigits stays at 1 (early-return path), 4 sig → 3 dec →
    // "0.123" with trailing strip preserving the visible precision.
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
    // 0 sig → clamped to 1 → 1 int digit → 0 dec → "10" after rounding 9.876.
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
