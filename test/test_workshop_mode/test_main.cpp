#include "workshop_mode_rs.h"

#include <stdint.h>
#include <string.h>
#include <unity.h>

namespace {

constexpr uint32_t kSixteenMbFilesystem = 0x760000;
constexpr uint32_t kFourMbFilesystem = 0xD0000;

void fillCounting(uint8_t *entropy, size_t len) {
    for (size_t i = 0; i < len; ++i)
        entropy[i] = static_cast<uint8_t>(i);
}

void test_entryWalksDrivingEnteringActive() {
    const uint8_t entering =
        workshop_next_state_rs(WORKSHOP_STATE_DRIVING, WORKSHOP_EVENT_REQUEST, true);
    TEST_ASSERT_EQUAL_UINT8(WORKSHOP_STATE_ENTERING, entering);
    TEST_ASSERT_EQUAL_UINT8(WORKSHOP_STATE_ACTIVE,
                            workshop_next_state_rs(entering, WORKSHOP_EVENT_ENTERED, false));
}

void test_requestIsRefusedWhenNotOffered() {
    TEST_ASSERT_EQUAL_UINT8(
        WORKSHOP_STATE_DRIVING,
        workshop_next_state_rs(WORKSHOP_STATE_DRIVING, WORKSHOP_EVENT_REQUEST, false));
}

void test_failedEntryReturnsToDriving() {
    TEST_ASSERT_EQUAL_UINT8(
        WORKSHOP_STATE_DRIVING,
        workshop_next_state_rs(WORKSHOP_STATE_ENTERING, WORKSHOP_EVENT_FAILED, false));
}

void test_onlyTheSixteenMegabyteLayoutIsSupported() {
    TEST_ASSERT_TRUE(workshop_board_supported_rs(kSixteenMbFilesystem));
    TEST_ASSERT_FALSE(workshop_board_supported_rs(kFourMbFilesystem));
}

void test_offeredWhileStoppedOrUnmapped() {
    TEST_ASSERT_TRUE(workshop_offered_rs(kSixteenMbFilesystem, true, true, 0.0f));
    TEST_ASSERT_TRUE(workshop_offered_rs(kSixteenMbFilesystem, false, false, 0.0f));
    TEST_ASSERT_FALSE(workshop_offered_rs(kSixteenMbFilesystem, true, true, 12.0f));
    TEST_ASSERT_FALSE(workshop_offered_rs(kFourMbFilesystem, true, true, 0.0f));
}

void test_generatedCredentialsAreValidAndJoinable() {
    uint8_t entropy[WORKSHOP_ENTROPY_BYTES];
    fillCounting(entropy, sizeof(entropy));
    char ssid[WORKSHOP_SSID_CAP];
    char password[WORKSHOP_PASSWORD_CAP];
    TEST_ASSERT_TRUE(workshop_generate_credentials_rs(entropy, sizeof(entropy), ssid, sizeof(ssid),
                                                      password, sizeof(password)));
    TEST_ASSERT_EQUAL_STRING("CANShift-ABCD", ssid);
    TEST_ASSERT_TRUE(workshop_credentials_valid_rs(ssid, password));

    char payload[WORKSHOP_JOIN_PAYLOAD_CAP];
    const size_t len = workshop_join_payload_rs(ssid, password, payload, sizeof(payload));
    TEST_ASSERT_EQUAL_STRING("WIFI:T:WPA;S:CANShift-ABCD;P:efghjkmnpqrs;;", payload);
    TEST_ASSERT_EQUAL_size_t(strlen(payload), len);
}

void test_storedGarbageIsRejected() {
    TEST_ASSERT_FALSE(workshop_credentials_valid_rs("", "abcdefgh"));
    TEST_ASSERT_FALSE(workshop_credentials_valid_rs("CANShift-ABCD", "short"));
}

} // namespace

void setUp() {}

void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_entryWalksDrivingEnteringActive);
    RUN_TEST(test_requestIsRefusedWhenNotOffered);
    RUN_TEST(test_failedEntryReturnsToDriving);
    RUN_TEST(test_onlyTheSixteenMegabyteLayoutIsSupported);
    RUN_TEST(test_offeredWhileStoppedOrUnmapped);
    RUN_TEST(test_generatedCredentialsAreValidAndJoinable);
    RUN_TEST(test_storedGarbageIsRejected);
    return UNITY_END();
}
