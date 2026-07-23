
#include "ui/top_bar_separator_link.h"

#include <unity.h>

using namespace TopBarSeparatorLink;

void setUp() {}
void tearDown() {}

void test_freshTracker_linkedFlagIsNoFlag() {
    Tracker t;
    TEST_ASSERT_EQUAL_INT8(NO_FLAG, linkedFlagForSeparator(t));
}

void test_registerModeFlag_setsLastFlag_noPending_returnsNoFlag() {
    Tracker t;
    const int8_t pending = registerModeFlag(t, 0, 1);
    TEST_ASSERT_EQUAL_INT8(NO_FLAG, pending);
    TEST_ASSERT_EQUAL_INT8(0, linkedFlagForSeparator(t));
}

void test_registerSeparator_thenModeFlag_returnsPendingSeparator() {
    Tracker t;
    registerModeFlag(t, 0, 1);
    TEST_ASSERT_EQUAL_INT8(0, linkedFlagForSeparator(t));
    registerSeparator(t, 1);
    const int8_t pending = registerModeFlag(t, 2, 3);
    TEST_ASSERT_EQUAL_INT8(1, pending);
    TEST_ASSERT_EQUAL_INT8(2, linkedFlagForSeparator(t));
}

void test_registerModeFlag_clearsPendingAfterConsuming() {
    Tracker t;
    registerSeparator(t, 4);
    TEST_ASSERT_EQUAL_INT8(4, registerModeFlag(t, 5, 6));
    TEST_ASSERT_EQUAL_INT8(NO_FLAG, registerModeFlag(t, 7, 8));
}

void test_registerModeFlag_pendingOutOfRange_returnsNoFlag() {
    Tracker t;
    registerSeparator(t, 9);
    TEST_ASSERT_EQUAL_INT8(NO_FLAG, registerModeFlag(t, 2, 3));
}

void test_resetTracking_clearsBothFields() {
    Tracker t;
    registerModeFlag(t, 0, 1);
    registerSeparator(t, 1);
    resetTracking(t);
    TEST_ASSERT_EQUAL_INT8(NO_FLAG, linkedFlagForSeparator(t));
    TEST_ASSERT_EQUAL_INT8(NO_FLAG, registerModeFlag(t, 3, 4));
}

void test_separatorWithoutPrecedingFlag_linkedIsNoFlag() {
    Tracker t;
    const int8_t linked = linkedFlagForSeparator(t);
    TEST_ASSERT_EQUAL_INT8(NO_FLAG, linked);
}

void test_hasValidLink_boundaries() {
    TEST_ASSERT_FALSE(hasValidLink(NO_FLAG, 4));
    TEST_ASSERT_FALSE(hasValidLink(4, 4));
    TEST_ASSERT_FALSE(hasValidLink(5, 4));
    TEST_ASSERT_TRUE(hasValidLink(0, 4));
    TEST_ASSERT_TRUE(hasValidLink(3, 4));
}

void test_wantsHidden_linkedFlagHidden_hides() {
    TEST_ASSERT_TRUE(wantsHidden(2, 4, true, false));
}

void test_wantsHidden_nextFlagHidden_hides() {
    TEST_ASSERT_TRUE(wantsHidden(2, 4, false, true));
}

void test_wantsHidden_nextFlagMissing_hides() {
    TEST_ASSERT_TRUE(wantsHidden(NO_FLAG, 4, false, false));
}

void test_wantsHidden_nextFlagOutOfRange_hides() {
    TEST_ASSERT_TRUE(wantsHidden(4, 4, false, false));
}

void test_wantsHidden_bothFlagsVisible_shows() {
    TEST_ASSERT_FALSE(wantsHidden(2, 4, false, false));
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_freshTracker_linkedFlagIsNoFlag);
    RUN_TEST(test_registerModeFlag_setsLastFlag_noPending_returnsNoFlag);
    RUN_TEST(test_registerSeparator_thenModeFlag_returnsPendingSeparator);
    RUN_TEST(test_registerModeFlag_clearsPendingAfterConsuming);
    RUN_TEST(test_registerModeFlag_pendingOutOfRange_returnsNoFlag);
    RUN_TEST(test_resetTracking_clearsBothFields);
    RUN_TEST(test_separatorWithoutPrecedingFlag_linkedIsNoFlag);
    RUN_TEST(test_hasValidLink_boundaries);
    RUN_TEST(test_wantsHidden_linkedFlagHidden_hides);
    RUN_TEST(test_wantsHidden_nextFlagHidden_hides);
    RUN_TEST(test_wantsHidden_nextFlagMissing_hides);
    RUN_TEST(test_wantsHidden_nextFlagOutOfRange_hides);
    RUN_TEST(test_wantsHidden_bothFlagsVisible_shows);
    return UNITY_END();
}
