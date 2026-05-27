// test_main.cpp — FIPS 180-4 test vectors for the software SHA-256 backend.
//
// Verifies sw_sha256 produces the canonical digests so we can trust it for
// the boot-time OTA key fingerprint path (where mbedTLS HW SHA races NimBLE
// and panics). Three vectors: empty, "abc", and the 56-byte two-block input.

#include "hal/wifi/sw_sha256.h"

#include <string.h>
#include <unity.h>

namespace {

using canshift::hal::wifi::sw_sha256_final;
using canshift::hal::wifi::sw_sha256_init;
using canshift::hal::wifi::sw_sha256_update;
using canshift::hal::wifi::SwSha256Ctx;

void hashBytes(const void *data, size_t len, uint8_t out[32]) {
    SwSha256Ctx ctx;
    sw_sha256_init(&ctx);
    sw_sha256_update(&ctx, static_cast<const uint8_t *>(data), len);
    sw_sha256_final(&ctx, out);
}

constexpr uint8_t kEmptyExpected[32] = {
    0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
    0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c, 0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55};

constexpr uint8_t kAbcExpected[32] = {
    0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
    0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};

constexpr uint8_t kTwoBlockExpected[32] = {
    0x24, 0x8d, 0x6a, 0x61, 0xd2, 0x06, 0x38, 0xb8, 0xe5, 0xc0, 0x26, 0x93, 0x0c, 0x3e, 0x60, 0x39,
    0xa3, 0x3c, 0xe4, 0x59, 0x64, 0xff, 0x21, 0x67, 0xf6, 0xec, 0xed, 0xd4, 0x19, 0xdb, 0x06, 0xc1};

} // namespace

void setUp() {}
void tearDown() {}

void test_emptyInput() {
    uint8_t out[32];
    hashBytes("", 0, out);
    TEST_ASSERT_EQUAL_MEMORY(kEmptyExpected, out, 32);
}

void test_abc() {
    uint8_t out[32];
    hashBytes("abc", 3, out);
    TEST_ASSERT_EQUAL_MEMORY(kAbcExpected, out, 32);
}

void test_twoBlockMessage() {
    const char *msg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    uint8_t out[32];
    hashBytes(msg, strlen(msg), out);
    TEST_ASSERT_EQUAL_MEMORY(kTwoBlockExpected, out, 32);
}

void test_chunkedUpdateMatchesOneShot() {
    const char *msg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    const size_t len = strlen(msg);

    uint8_t oneShot[32];
    hashBytes(msg, len, oneShot);

    uint8_t chunked[32];
    SwSha256Ctx ctx;
    sw_sha256_init(&ctx);
    sw_sha256_update(&ctx, reinterpret_cast<const uint8_t *>(msg), 1);
    sw_sha256_update(&ctx, reinterpret_cast<const uint8_t *>(msg + 1), 32);
    sw_sha256_update(&ctx, reinterpret_cast<const uint8_t *>(msg + 33), len - 33);
    sw_sha256_final(&ctx, chunked);

    TEST_ASSERT_EQUAL_MEMORY(oneShot, chunked, 32);
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_emptyInput);
    RUN_TEST(test_abc);
    RUN_TEST(test_twoBlockMessage);
    RUN_TEST(test_chunkedUpdateMatchesOneShot);
    return UNITY_END();
}
