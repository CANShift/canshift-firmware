#include "ui/gesture_intent.h"

#include <unity.h>

using GestureIntent::decide;
using GestureIntent::Decision;

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

void test_offAClickableAnyHorizontalTravelSwipes() {
    const Decision right = decide(20, 40, false);
    TEST_ASSERT_TRUE(right.cancelClick);
    TEST_ASSERT_TRUE(right.fireSwipe);
    TEST_ASSERT_FALSE(right.swipeLeft);
}

void test_directionFollowsTheSignOfTravel() {
    TEST_ASSERT_TRUE(decide(-60, 0, true).swipeLeft);
    TEST_ASSERT_FALSE(decide(60, 0, true).swipeLeft);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_aTapIsNeitherCancelledNorASwipe);
    RUN_TEST(test_aCleanHorizontalSwipeOffAButtonCancelsAndFires);
    RUN_TEST(test_aDiagonalSwipeOffAButtonCancelsTheClickWithoutChangingPage);
    RUN_TEST(test_aVerticalDragOffAButtonCancelsTheClick);
    RUN_TEST(test_aShortWobbleStaysAClick);
    RUN_TEST(test_offAClickableAnyHorizontalTravelSwipes);
    RUN_TEST(test_directionFollowsTheSignOfTravel);
    return UNITY_END();
}
