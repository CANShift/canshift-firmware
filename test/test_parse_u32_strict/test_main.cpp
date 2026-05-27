// test_main.cpp — Unity tests for the parseU32Strict helper (parse_utils.h).
//
// The helper lives in a header so it can be included directly here without
// going through config_loader's internal linkage.

#include "config/parse_utils.h"

#include <unity.h>

void setUp() {}
void tearDown() {}

void test_parseU32Strict_validHexWithPrefix_returnsTrue() {
    uint32_t out = 0;
    TEST_ASSERT_TRUE(parseU32Strict("0x1A", 16, &out));
    TEST_ASSERT_EQUAL_UINT32(0x1Au, out);
}

void test_parseU32Strict_validHexUppercase_returnsTrue() {
    uint32_t out = 0;
    TEST_ASSERT_TRUE(parseU32Strict("0x370", 16, &out));
    TEST_ASSERT_EQUAL_UINT32(0x370u, out);
}

void test_parseU32Strict_invalidHexChars_returnsFalse() {
    uint32_t out = 0xDEAD;
    TEST_ASSERT_FALSE(parseU32Strict("0xGG", 16, &out));
    // out must not have been written with a garbage value
    TEST_ASSERT_EQUAL_UINT32(0xDEADu, out);
}

void test_parseU32Strict_emptyString_returnsFalse() {
    uint32_t out = 0;
    TEST_ASSERT_FALSE(parseU32Strict("", 16, &out));
}

void test_parseU32Strict_nullPointer_returnsFalse() {
    uint32_t out = 0;
    TEST_ASSERT_FALSE(parseU32Strict(nullptr, 16, &out));
}

void test_parseU32Strict_trailingGarbage_returnsFalse() {
    uint32_t out = 0;
    TEST_ASSERT_FALSE(parseU32Strict("0x1A trailing", 16, &out));
}

void test_parseU32Strict_trailingSpaceAfterHex_returnsFalse() {
    uint32_t out = 0;
    TEST_ASSERT_FALSE(parseU32Strict("0x1A ", 16, &out));
}

void test_parseU32Strict_mixedBaseDecimal_returnsFalse() {
    uint32_t out = 0;
    TEST_ASSERT_FALSE(parseU32Strict("123abc", 10, &out));
}

void test_parseU32Strict_validDecimal_returnsTrue() {
    uint32_t out = 0;
    TEST_ASSERT_TRUE(parseU32Strict("255", 10, &out));
    TEST_ASSERT_EQUAL_UINT32(255u, out);
}

void test_parseU32Strict_uint32Max_returnsTrue() {
    uint32_t out = 0;
    TEST_ASSERT_TRUE(parseU32Strict("4294967295", 10, &out));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, out);
}

void test_parseU32Strict_uint32MaxPlusOne_returnsFalse() {
    uint32_t out = 0;
    // 4294967296 == UINT32_MAX + 1 — must overflow
    TEST_ASSERT_FALSE(parseU32Strict("4294967296", 10, &out));
}

void test_parseU32Strict_autoBase0_hexPrefix_returnsTrue() {
    uint32_t out = 0;
    TEST_ASSERT_TRUE(parseU32Strict("0x1A", 0, &out));
    TEST_ASSERT_EQUAL_UINT32(0x1Au, out);
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_parseU32Strict_validHexWithPrefix_returnsTrue);
    RUN_TEST(test_parseU32Strict_validHexUppercase_returnsTrue);
    RUN_TEST(test_parseU32Strict_invalidHexChars_returnsFalse);
    RUN_TEST(test_parseU32Strict_emptyString_returnsFalse);
    RUN_TEST(test_parseU32Strict_nullPointer_returnsFalse);
    RUN_TEST(test_parseU32Strict_trailingGarbage_returnsFalse);
    RUN_TEST(test_parseU32Strict_trailingSpaceAfterHex_returnsFalse);
    RUN_TEST(test_parseU32Strict_mixedBaseDecimal_returnsFalse);
    RUN_TEST(test_parseU32Strict_validDecimal_returnsTrue);
    RUN_TEST(test_parseU32Strict_uint32Max_returnsTrue);
    RUN_TEST(test_parseU32Strict_uint32MaxPlusOne_returnsFalse);
    RUN_TEST(test_parseU32Strict_autoBase0_hexPrefix_returnsTrue);
    return UNITY_END();
}
