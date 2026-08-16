#include "control_state_rs.h"
#include "ui/control_vocabulary.h"

#include <string.h>
#include <unity.h>

namespace {

using ControlVocabulary::Control;
using ControlVocabulary::ControlState;

struct MatrixRow {
    const char *kicker;
    ControlState state;
    int param;
    const char *expectedKicker;
    const char *expectedWord;
};

constexpr MatrixRow kMatrix[] = {
    {"ANTI-LAG", ControlState::OFF, 0, "ANTI-LAG", "OFF"},
    {"ANTI-LAG", ControlState::ACTIVE, 0, "ANTI-LAG", "ON"},
    {"ANTI-LAG", ControlState::UNAVAILABLE, 0, "ANTI-LAG · EGT HIGH", "LOCKED"},
    {"TRACTION", ControlState::OFF, 0, "TRACTION", "OFF"},
    {"TRACTION", ControlState::ARMED, 4, "TRACTION", "LEVEL 4"},
    {"TRACTION", ControlState::ACTIVE, 4, "TRACTION · CUTTING", "LEVEL 4"},
    {"TRACTION", ControlState::UNAVAILABLE, 0, "TRACTION · NO WHEEL SPEED", "N/A"},
    {"LAUNCH", ControlState::OFF, 0, "LAUNCH", "OFF"},
    {"LAUNCH", ControlState::ARMED, 4200, "LAUNCH · 4200 rpm", "ARMED"},
    {"LAUNCH", ControlState::ACTIVE, 4200, "LAUNCH · HOLDING", "4200"},
    {"LAUNCH", ControlState::UNAVAILABLE, 0, "LAUNCH · MOVING", "LOCKED"},
    {"PIT LIMIT", ControlState::OFF, 0, "PIT LIMIT", "OFF"},
    {"PIT LIMIT", ControlState::ACTIVE, 60, "PIT LIMIT · HOLDING", "60"},
    {"PIT LIMIT", ControlState::UNAVAILABLE, 4, "PIT LIMIT · GEAR 4", "LOCKED"},
    {"CRUISE", ControlState::OFF, 0, "CRUISE", "OFF"},
    {"CRUISE", ControlState::ARMED, 110, "CRUISE · SET 110", "ARMED"},
    {"CRUISE", ControlState::ACTIVE, 110, "CRUISE · HOLDING", "110"},
    {"CRUISE", ControlState::UNAVAILABLE, 0, "CRUISE · BRAKE CUT", "CANCELLED"},
};

void test_matrix_matchesTheReference() {
    char kicker[48];
    char word[48];
    for (const MatrixRow &row : kMatrix) {
        const Control *control = ControlVocabulary::find(row.kicker);
        TEST_ASSERT_NOT_NULL(control);
        ControlVocabulary::composeKicker(*control, row.state, row.param, kicker, sizeof(kicker));
        ControlVocabulary::composeStateWord(*control, row.state, row.param, word, sizeof(word));
        TEST_ASSERT_EQUAL_STRING(row.expectedKicker, kicker);
        TEST_ASSERT_EQUAL_STRING(row.expectedWord, word);
    }
}

void test_find_rejectsUnknownKickers() {
    TEST_ASSERT_NULL(ControlVocabulary::find("TIMER"));
    TEST_ASSERT_NULL(ControlVocabulary::find(""));
    TEST_ASSERT_NULL(ControlVocabulary::find(nullptr));
}

void test_kinds_splitTogglesFromSteppers() {
    TEST_ASSERT_EQUAL(ControlVocabulary::ControlKind::STEPPER,
                      ControlVocabulary::find("TRACTION")->kind);
    TEST_ASSERT_EQUAL(ControlVocabulary::ControlKind::STEPPER,
                      ControlVocabulary::find("ECU MAP")->kind);
    TEST_ASSERT_EQUAL(ControlVocabulary::ControlKind::TOGGLE,
                      ControlVocabulary::find("CRUISE")->kind);
    TEST_ASSERT_EQUAL_UINT8(0, ControlVocabulary::find("TRACTION")->stepFloor);
    TEST_ASSERT_EQUAL_UINT8(1, ControlVocabulary::find("ECU MAP")->stepFloor);
}

void test_compose_truncatesWithoutOverrun() {
    char small[8];
    const Control *control = ControlVocabulary::find("TRACTION");
    ControlVocabulary::composeKicker(*control, ControlState::UNAVAILABLE, 0, small, sizeof(small));
    TEST_ASSERT_EQUAL_STRING("TRACTIO", small);
}

void test_stepperTapClimbsThenWrapsToOff() {
    ControlStepperRs stepper = {};
    control_stepper_init_rs(&stepper, 0);
    for (uint8_t expected = 1; expected <= CONTROL_STEP_MAX; ++expected) {
        control_stepper_press_rs(&stepper, expected * 10u);
        TEST_ASSERT_TRUE(control_stepper_release_rs(&stepper, expected * 10u + 5u));
        TEST_ASSERT_EQUAL_UINT8(expected, stepper.level);
    }
    control_stepper_press_rs(&stepper, 1000);
    TEST_ASSERT_TRUE(control_stepper_release_rs(&stepper, 1010));
    TEST_ASSERT_EQUAL_UINT8(0, stepper.level);
}

void test_stepperLongPressReturnsToLevelOne() {
    ControlStepperRs stepper = {};
    control_stepper_init_rs(&stepper, 5);
    control_stepper_press_rs(&stepper, 2000);
    TEST_ASSERT_FALSE(control_stepper_poll_rs(&stepper, 2000 + CONTROL_LONG_PRESS_MS - 1));
    TEST_ASSERT_TRUE(control_stepper_poll_rs(&stepper, 2000 + CONTROL_LONG_PRESS_MS));
    TEST_ASSERT_EQUAL_UINT8(1, stepper.level);
    TEST_ASSERT_FALSE(control_stepper_release_rs(&stepper, 3000));
    TEST_ASSERT_EQUAL_UINT8(1, stepper.level);
}

void test_resolveOrdersTheFourStates() {
    TEST_ASSERT_EQUAL_UINT8(CONTROL_STATE_UNAVAILABLE,
                            control_state_resolve_rs(true, true, true, true));
    TEST_ASSERT_EQUAL_UINT8(CONTROL_STATE_ACTIVE,
                            control_state_resolve_rs(false, true, true, true));
    TEST_ASSERT_EQUAL_UINT8(CONTROL_STATE_ARMED,
                            control_state_resolve_rs(false, false, true, true));
    TEST_ASSERT_EQUAL_UINT8(CONTROL_STATE_OFF, control_state_resolve_rs(false, false, false, true));
}

void test_binaryControlSkipsArmed() {
    TEST_ASSERT_EQUAL_UINT8(CONTROL_STATE_ACTIVE,
                            control_state_resolve_rs(false, false, true, false));
    TEST_ASSERT_EQUAL_UINT8(CONTROL_STATE_OFF,
                            control_state_resolve_rs(false, false, false, false));
}

void test_armedIsDeclaredPerControl() {
    TEST_ASSERT_FALSE(ControlVocabulary::hasArmedState(*ControlVocabulary::find("ANTI-LAG")));
    TEST_ASSERT_FALSE(ControlVocabulary::hasArmedState(*ControlVocabulary::find("PIT LIMIT")));
    TEST_ASSERT_FALSE(ControlVocabulary::hasArmedState(*ControlVocabulary::find("ECU MAP")));
    TEST_ASSERT_TRUE(ControlVocabulary::hasArmedState(*ControlVocabulary::find("LAUNCH")));
    TEST_ASSERT_TRUE(ControlVocabulary::hasArmedState(*ControlVocabulary::find("CRUISE")));
    TEST_ASSERT_TRUE(ControlVocabulary::hasArmedState(*ControlVocabulary::find("TRACTION")));
}

} // namespace

void setUp() {}

void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_matrix_matchesTheReference);
    RUN_TEST(test_find_rejectsUnknownKickers);
    RUN_TEST(test_kinds_splitTogglesFromSteppers);
    RUN_TEST(test_compose_truncatesWithoutOverrun);
    RUN_TEST(test_stepperTapClimbsThenWrapsToOff);
    RUN_TEST(test_stepperLongPressReturnsToLevelOne);
    RUN_TEST(test_resolveOrdersTheFourStates);
    RUN_TEST(test_binaryControlSkipsArmed);
    RUN_TEST(test_armedIsDeclaredPerControl);
    return UNITY_END();
}
