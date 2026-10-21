#include "can_parser_rs.h"

#include <unity.h>

namespace {

void test_runningBusNeedsNothing() {
    TEST_ASSERT_EQUAL_UINT8(kCanBusActionNone, can_bus_action_rs(kCanBusStateRunning, 0));
}

void test_busOffStartsRecovery() {
    TEST_ASSERT_EQUAL_UINT8(kCanBusActionRecover, can_bus_action_rs(kCanBusStateBusOff, 0));
}

void test_recoveringWaitsForTheDriver() {
    TEST_ASSERT_EQUAL_UINT8(kCanBusActionNone, can_bus_action_rs(kCanBusStateRecovering, 0));
}

void test_stoppedAfterRecoveryRestartsTheBus() {
    TEST_ASSERT_EQUAL_UINT8(kCanBusActionRestart, can_bus_action_rs(kCanBusStateStopped, 0));
}

void test_aBusOffSequenceEndsRunning() {
    const uint8_t sequence[] = {kCanBusStateRunning, kCanBusStateBusOff, kCanBusStateRecovering,
                                kCanBusStateStopped, kCanBusStateRunning};
    const uint8_t expected[] = {kCanBusActionNone, kCanBusActionRecover, kCanBusActionNone,
                                kCanBusActionRestart, kCanBusActionNone};
    for (size_t i = 0; i < sizeof(sequence); ++i) {
        TEST_ASSERT_EQUAL_UINT8(expected[i], can_bus_action_rs(sequence[i], 0));
    }
}

void test_unknownStateIsLeftAlone() {
    TEST_ASSERT_EQUAL_UINT8(kCanBusActionNone, can_bus_action_rs(9, 0));
}

void test_recoveryGivesUpOnceAfterTheCap() {
    uint8_t recoveries = 0;
    while (recoveries < UINT8_MAX &&
           can_bus_action_rs(kCanBusStateBusOff, recoveries) == kCanBusActionRecover)
        ++recoveries;
    TEST_ASSERT_GREATER_THAN_UINT8(0, recoveries);
    TEST_ASSERT_EQUAL_UINT8(kCanBusActionGiveUp, can_bus_action_rs(kCanBusStateBusOff, recoveries));
    TEST_ASSERT_EQUAL_UINT8(kCanBusActionNone,
                            can_bus_action_rs(kCanBusStateBusOff, recoveries + 1));
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
    RUN_TEST(test_recoveryGivesUpOnceAfterTheCap);
    return UNITY_END();
}
