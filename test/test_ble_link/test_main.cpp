#include "ble_link_rs.h"

#include <stdint.h>
#include <unity.h>

namespace {

constexpr uint32_t kDeadlineMs = 20000;

void test_passkeyPairingIsAdmitted() {
    TEST_ASSERT_TRUE(ble_link_admitted_rs(true, true));
}

void test_encryptedLinkWithoutPasskeyIsNotAdmitted() {
    TEST_ASSERT_FALSE(ble_link_admitted_rs(true, false));
}

void test_openLinkIsNotAdmitted() {
    TEST_ASSERT_FALSE(ble_link_admitted_rs(false, false));
    TEST_ASSERT_FALSE(ble_link_admitted_rs(false, true));
}

void test_subscriberOverJustWorksBondIsRefused() {
    TEST_ASSERT_TRUE(ble_link_subscription_refused_rs(true, false));
}

void test_subscriberBeforePairingIsKeptForThePasskey() {
    TEST_ASSERT_FALSE(ble_link_subscription_refused_rs(false, false));
    TEST_ASSERT_FALSE(ble_link_subscription_refused_rs(true, true));
}

void test_unauthenticatedPeerIsDroppedAtTheDeadline() {
    TEST_ASSERT_FALSE(ble_link_pairing_overdue_rs(false, false, kDeadlineMs - 1));
    TEST_ASSERT_TRUE(ble_link_pairing_overdue_rs(false, false, kDeadlineMs));
    TEST_ASSERT_TRUE(ble_link_pairing_overdue_rs(true, false, kDeadlineMs));
}

void test_authenticatedPeerStaysPastTheDeadline() {
    TEST_ASSERT_FALSE(ble_link_pairing_overdue_rs(true, true, kDeadlineMs));
    TEST_ASSERT_FALSE(ble_link_pairing_overdue_rs(true, true, UINT32_MAX));
}

} // namespace

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_passkeyPairingIsAdmitted);
    RUN_TEST(test_encryptedLinkWithoutPasskeyIsNotAdmitted);
    RUN_TEST(test_openLinkIsNotAdmitted);
    RUN_TEST(test_subscriberOverJustWorksBondIsRefused);
    RUN_TEST(test_subscriberBeforePairingIsKeptForThePasskey);
    RUN_TEST(test_unauthenticatedPeerIsDroppedAtTheDeadline);
    RUN_TEST(test_authenticatedPeerStaysPastTheDeadline);
    return UNITY_END();
}
