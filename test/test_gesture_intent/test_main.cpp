#include "ui/gesture_intent.h"

#include <unity.h>

using GestureIntent::closesSettings;
using GestureIntent::decide;
using GestureIntent::Decision;
using GestureIntent::kTapTravelLimitPx;
using GestureIntent::TapTurn;
using GestureIntent::tapTurn;

void setUp() {}
void tearDown() {}

void test_aTapIsNeitherCancelledNorASwipe() {
    const Decision d = decide(0, 0, true);
    TEST_ASSERT_FALSE(d.cancelClick);
    TEST_ASSERT_FALSE(d.fireSwipe);
}

void test_aCleanHorizontalSwipeOffAButtonCancelsAndFires() {
    const Decision d = decide(-60, 10, true);
    TEST_ASSERT_TRUE(d.cancelClick);
    TEST_ASSERT_TRUE(d.fireSwipe);
    TEST_ASSERT_TRUE(d.swipeLeft);
}

void test_aDiagonalSwipeOffAButtonCancelsTheClickWithoutChangingPage() {
    const Decision d = decide(40, 25, true);
    TEST_ASSERT_TRUE(d.cancelClick);
    TEST_ASSERT_FALSE(d.fireSwipe);
}

void test_aVerticalDragOffAButtonCancelsTheClick() {
    const Decision d = decide(2, 50, true);
    TEST_ASSERT_TRUE(d.cancelClick);
    TEST_ASSERT_FALSE(d.fireSwipe);
}

void test_aShortWobbleStaysAClick() {
    const Decision d = decide(6, 6, true);
    TEST_ASSERT_FALSE(d.cancelClick);
    TEST_ASSERT_FALSE(d.fireSwipe);
}

void test_offAClickableHorizontallyDominatedTravelSwipes() {
    const Decision right = decide(20, 5, false);
    TEST_ASSERT_TRUE(right.cancelClick);
    TEST_ASSERT_TRUE(right.fireSwipe);
    TEST_ASSERT_FALSE(right.swipeLeft);

    const Decision vertical = decide(20, 40, false);
    TEST_ASSERT_TRUE(vertical.cancelClick);
    TEST_ASSERT_FALSE(vertical.fireSwipe);
}

void test_directionFollowsTheSignOfTravel() {
    TEST_ASSERT_TRUE(decide(-60, 0, true).swipeLeft);
    TEST_ASSERT_FALSE(decide(60, 0, true).swipeLeft);
}

void test_closesSettings_upwardSwipePastThreshold() {
    TEST_ASSERT_TRUE(closesSettings(-40, 0));
    TEST_ASSERT_TRUE(closesSettings(-32, 5));
}

void test_closesSettings_downwardTravelNeverCloses() {
    TEST_ASSERT_FALSE(closesSettings(40, 0));
    TEST_ASSERT_FALSE(closesSettings(100, 0));
}

void test_closesSettings_shortTravelIsNotASwipe() {
    TEST_ASSERT_FALSE(closesSettings(-31, 0));
    TEST_ASSERT_FALSE(closesSettings(-5, 0));
}

void test_closesSettings_horizontalDominatedTravelIsNotAClose() {
    TEST_ASSERT_FALSE(closesSettings(-40, 40));
    TEST_ASSERT_FALSE(closesSettings(-40, -40));
    TEST_ASSERT_FALSE(closesSettings(-40, 20));
}

void test_decide_verticalSwipeOffClickableDoesNotTurnThePage() {
    TEST_ASSERT_FALSE(decide(8, 100, false).fireSwipe);
    TEST_ASSERT_FALSE(decide(-8, 100, false).fireSwipe);
    TEST_ASSERT_FALSE(decide(30, 60, false).fireSwipe);
}

void test_decide_verticalSwipeOffClickableStillCancelsTheClick() {
    TEST_ASSERT_TRUE(decide(8, 100, false).cancelClick);
}

void test_decide_horizontalSwipeOffClickableStillTurnsThePage() {
    TEST_ASSERT_TRUE(decide(60, 10, false).fireSwipe);
    TEST_ASSERT_TRUE(decide(-60, 10, false).fireSwipe);
    TEST_ASSERT_TRUE(decide(20, 5, false).fireSwipe);
}

constexpr int16_t kScreenWidthPx = 320;

void test_tapTurn_rightThirdTurnsToTheNextPage() {
    TEST_ASSERT_TRUE(tapTurn(300, kScreenWidthPx, 0) == TapTurn::Next);
    TEST_ASSERT_TRUE(tapTurn(214, kScreenWidthPx, 0) == TapTurn::Next);
    TEST_ASSERT_TRUE(tapTurn(319, kScreenWidthPx, 0) == TapTurn::Next);
}

void test_tapTurn_leftThirdTurnsToThePreviousPage() {
    TEST_ASSERT_TRUE(tapTurn(0, kScreenWidthPx, 0) == TapTurn::Previous);
    TEST_ASSERT_TRUE(tapTurn(105, kScreenWidthPx, 0) == TapTurn::Previous);
}

void test_tapTurn_middleThirdDoesNothing() {
    TEST_ASSERT_TRUE(tapTurn(106, kScreenWidthPx, 0) == TapTurn::None);
    TEST_ASSERT_TRUE(tapTurn(160, kScreenWidthPx, 0) == TapTurn::None);
    TEST_ASSERT_TRUE(tapTurn(213, kScreenWidthPx, 0) == TapTurn::None);
}

void test_tapTurn_travelAtTheTapLimitIsNotATap() {
    TEST_ASSERT_TRUE(tapTurn(300, kScreenWidthPx, kTapTravelLimitPx - 1) == TapTurn::Next);
    TEST_ASSERT_TRUE(tapTurn(300, kScreenWidthPx, kTapTravelLimitPx) == TapTurn::None);
    TEST_ASSERT_TRUE(tapTurn(10, kScreenWidthPx, 80) == TapTurn::None);
}

void test_tapTurn_followsTheScreenWidth() {
    TEST_ASSERT_TRUE(tapTurn(300, 480, 0) == TapTurn::None);
    TEST_ASSERT_TRUE(tapTurn(400, 480, 0) == TapTurn::Next);
}

void test_tapTurn_degenerateWidthNeverTurns() {
    TEST_ASSERT_TRUE(tapTurn(0, 0, 0) == TapTurn::None);
    TEST_ASSERT_TRUE(tapTurn(1, 2, 0) == TapTurn::None);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_aTapIsNeitherCancelledNorASwipe);
    RUN_TEST(test_aCleanHorizontalSwipeOffAButtonCancelsAndFires);
    RUN_TEST(test_aDiagonalSwipeOffAButtonCancelsTheClickWithoutChangingPage);
    RUN_TEST(test_aVerticalDragOffAButtonCancelsTheClick);
    RUN_TEST(test_aShortWobbleStaysAClick);
    RUN_TEST(test_offAClickableHorizontallyDominatedTravelSwipes);
    RUN_TEST(test_directionFollowsTheSignOfTravel);
    RUN_TEST(test_closesSettings_upwardSwipePastThreshold);
    RUN_TEST(test_closesSettings_downwardTravelNeverCloses);
    RUN_TEST(test_closesSettings_shortTravelIsNotASwipe);
    RUN_TEST(test_closesSettings_horizontalDominatedTravelIsNotAClose);
    RUN_TEST(test_decide_verticalSwipeOffClickableDoesNotTurnThePage);
    RUN_TEST(test_decide_verticalSwipeOffClickableStillCancelsTheClick);
    RUN_TEST(test_decide_horizontalSwipeOffClickableStillTurnsThePage);
    RUN_TEST(test_tapTurn_rightThirdTurnsToTheNextPage);
    RUN_TEST(test_tapTurn_leftThirdTurnsToThePreviousPage);
    RUN_TEST(test_tapTurn_middleThirdDoesNothing);
    RUN_TEST(test_tapTurn_travelAtTheTapLimitIsNotATap);
    RUN_TEST(test_tapTurn_followsTheScreenWidth);
    RUN_TEST(test_tapTurn_degenerateWidthNeverTurns);
    return UNITY_END();
}
