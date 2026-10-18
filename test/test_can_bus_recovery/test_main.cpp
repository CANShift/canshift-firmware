#include "can_parser_rs.h"

#include <unity.h>

namespace {

void test_runningBusNeedsNothing() {
    TEST_ASSERT_EQUAL_UINT8(kCanBusActionNone, can_bus_action_rs(kCanBusStateRunning));
}

void test_busOffStartsRecovery() {
    TEST_ASSERT_EQUAL_UINT8(kCanBusActionRecover, can_bus_action_rs(kCanBusStateBusOff));
}

void test_recoveringWaitsForTheDriver() {
    TEST_ASSERT_EQUAL_UINT8(kCanBusActionNone, can_bus_action_rs(kCanBusStateRecovering));
}

void test_stoppedAfterRecoveryRestartsTheBus() {
    TEST_ASSERT_EQUAL_UINT8(kCanBusActionRestart, can_bus_action_rs(kCanBusStateStopped));
}

void test_aBusOffSequenceEndsRunning() {
    const uint8_t sequence[] = {kCanBusStateRunning, kCanBusStateBusOff, kCanBusStateRecovering,
                                kCanBusStateStopped, kCanBusStateRunning};
    const uint8_t expected[] = {kCanBusActionNone, kCanBusActionRecover, kCanBusActionNone,
                                kCanBusActionRestart, kCanBusActionNone};
    for (size_t i = 0; i < sizeof(sequence); ++i) {
        TEST_ASSERT_EQUAL_UINT8(expected[i], can_bus_action_rs(sequence[i]));
    }
}

void test_unknownStateIsLeftAlone() {
    TEST_ASSERT_EQUAL_UINT8(kCanBusActionNone, can_bus_action_rs(9));
}

}

void setUp() {}

void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_runningBusNeedsNothing);
    RUN_TEST(test_busOffStartsRecovery);
    RUN_TEST(test_recoveringWaitsForTheDriver);
    RUN_TEST(test_stoppedAfterRecoveryRestartsTheBus);
    RUN_TEST(test_aBusOffSequenceEndsRunning);
    RUN_TEST(test_unknownStateIsLeftAlone);
    return UNITY_END();
}
