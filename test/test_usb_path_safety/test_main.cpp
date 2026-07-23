
#include "hal/usb/usb_dispatch_validation.h"

#include <string.h>
#include <unity.h>

using namespace UsbDispatchValidation;

void setUp() {}
void tearDown() {}

void test_isPathSafe_allowedAssetsPrefix_returnsTrue() {
    TEST_ASSERT_TRUE(isPathSafe("/assets/icon.png"));
}

void test_isPathSafe_allowedFontsPrefix_returnsTrue() {
    TEST_ASSERT_TRUE(isPathSafe("/fonts/orbitron.bin"));
}

void test_isPathSafe_pathTraversal_returnsFalse() {
    TEST_ASSERT_FALSE(isPathSafe("/assets/../secret.bin"));
}

void test_isPathSafe_absolutePathOutsideAllowlist_returnsFalse() {
    TEST_ASSERT_FALSE(isPathSafe("/etc/passwd"));
}

void test_isPathSafe_doubleSlash_returnsFalse() {
    TEST_ASSERT_FALSE(isPathSafe("/assets//icon.png"));
}

void test_isPathSafe_prefixOnlyNoLeaf_returnsFalse() {
    TEST_ASSERT_FALSE(isPathSafe("/assets/"));
}

void test_isPathSafe_emptyInput_returnsFalse() {
    TEST_ASSERT_FALSE(isPathSafe(""));
}

void test_isPathSafe_nullptr_returnsFalse() {
    TEST_ASSERT_FALSE(isPathSafe(nullptr));
}

void test_isPathSafe_controlCharacter_returnsFalse() {
    TEST_ASSERT_FALSE(isPathSafe("/assets/ic\ton.png"));
}

void test_parseSha256Hex_validLowercase_returnsTrue() {
    const char *hex = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    uint8_t out[SHA256_BYTES] = {0};
    TEST_ASSERT_TRUE(parseSha256Hex(hex, out));
    TEST_ASSERT_EQUAL_UINT8(0x01, out[0]);
    TEST_ASSERT_EQUAL_UINT8(0x23, out[1]);
    TEST_ASSERT_EQUAL_UINT8(0xEF, out[SHA256_BYTES - 1]);
}

void test_parseSha256Hex_validUppercase_returnsTrue() {
    const char *hex = "ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789";
    uint8_t out[SHA256_BYTES] = {0};
    TEST_ASSERT_TRUE(parseSha256Hex(hex, out));
    TEST_ASSERT_EQUAL_UINT8(0xAB, out[0]);
}

void test_parseSha256Hex_oddLength_returnsFalse() {
    const char *hex = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcde";
    uint8_t out[SHA256_BYTES] = {0};
    TEST_ASSERT_EQUAL_UINT32(63u, strlen(hex));
    TEST_ASSERT_FALSE(parseSha256Hex(hex, out));
}

void test_parseSha256Hex_nonHexChars_returnsFalse() {
    const char *hex = "zz23456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    uint8_t out[SHA256_BYTES] = {0};
    TEST_ASSERT_FALSE(parseSha256Hex(hex, out));
}

void test_parseSha256Hex_emptyInput_returnsFalse() {
    uint8_t out[SHA256_BYTES] = {0};
    TEST_ASSERT_FALSE(parseSha256Hex("", out));
}

void test_parseSha256Hex_nullptr_returnsFalse() {
    uint8_t out[SHA256_BYTES] = {0};
    TEST_ASSERT_FALSE(parseSha256Hex(nullptr, out));
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_isPathSafe_allowedAssetsPrefix_returnsTrue);
    RUN_TEST(test_isPathSafe_allowedFontsPrefix_returnsTrue);
    RUN_TEST(test_isPathSafe_pathTraversal_returnsFalse);
    RUN_TEST(test_isPathSafe_absolutePathOutsideAllowlist_returnsFalse);
    RUN_TEST(test_isPathSafe_doubleSlash_returnsFalse);
    RUN_TEST(test_isPathSafe_prefixOnlyNoLeaf_returnsFalse);
    RUN_TEST(test_isPathSafe_emptyInput_returnsFalse);
    RUN_TEST(test_isPathSafe_nullptr_returnsFalse);
    RUN_TEST(test_isPathSafe_controlCharacter_returnsFalse);
    RUN_TEST(test_parseSha256Hex_validLowercase_returnsTrue);
    RUN_TEST(test_parseSha256Hex_validUppercase_returnsTrue);
    RUN_TEST(test_parseSha256Hex_oddLength_returnsFalse);
    RUN_TEST(test_parseSha256Hex_nonHexChars_returnsFalse);
    RUN_TEST(test_parseSha256Hex_emptyInput_returnsFalse);
    RUN_TEST(test_parseSha256Hex_nullptr_returnsFalse);
    return UNITY_END();
}
