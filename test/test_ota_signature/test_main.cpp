#include "ota_signature_rs.h"

#include <stdint.h>
#include <string.h>
#include <unity.h>

namespace {

constexpr uint8_t kForeignSignatureByte = 0x11;
constexpr uint8_t kImage[] = {0xe9, 0x01, 0x02, 0x03};

OtaSignatureSlot clearedSlot() {
    OtaSignatureSlot slot;
    ota_signature_clear_rs(&slot);
    return slot;
}

void test_clearedSlotNeverVerifies() {
    OtaSignatureSlot slot = clearedSlot();
    ota_signature_update_rs(&slot, kImage, sizeof(kImage));
    TEST_ASSERT_FALSE(ota_signature_finish_rs(&slot));
}

void test_missingSignatureDoesNotBegin() {
    OtaSignatureSlot slot = clearedSlot();
    TEST_ASSERT_FALSE(ota_signature_begin_rs(&slot, nullptr));
    TEST_ASSERT_FALSE(ota_signature_finish_rs(&slot));
}

void test_nonCanonicalSignatureIsRefusedAtBegin() {
    OtaSignatureSlot slot = clearedSlot();
    uint8_t signature[kOtaSignatureBytes];
    memset(signature, 0xff, sizeof(signature));
    TEST_ASSERT_FALSE(ota_signature_begin_rs(&slot, signature));
}

void test_foreignSignatureFailsAgainstTheReleaseKey() {
    OtaSignatureSlot slot = clearedSlot();
    uint8_t signature[kOtaSignatureBytes];
    memset(signature, kForeignSignatureByte, sizeof(signature));
    signature[kOtaSignatureBytes - 1] = 0;
    TEST_ASSERT_TRUE(ota_signature_begin_rs(&slot, signature));
    ota_signature_update_rs(&slot, kImage, sizeof(kImage));
    TEST_ASSERT_FALSE(ota_signature_finish_rs(&slot));
}

} // namespace

void setUp() {}

void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_clearedSlotNeverVerifies);
    RUN_TEST(test_missingSignatureDoesNotBegin);
    RUN_TEST(test_nonCanonicalSignatureIsRefusedAtBegin);
    RUN_TEST(test_foreignSignatureFailsAgainstTheReleaseKey);
    return UNITY_END();
}
