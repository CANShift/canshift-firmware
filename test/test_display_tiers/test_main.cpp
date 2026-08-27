#include "display_tiers.h"

#include <unity.h>

using namespace canshift::display;

void setUp() {}
void tearDown() {}

void test_baseTierIsTheDesignSpaceEveryPageIsAuthoredIn() {
    TEST_ASSERT_EQUAL_STRING("base", kBaseTier.id);
    TEST_ASSERT_EQUAL_UINT16(320, kBaseTier.designWidth);
    TEST_ASSERT_EQUAL_UINT16(240, kBaseTier.designHeight);
    TEST_ASSERT_EQUAL_UINT8(12, kBaseTier.columns);
    TEST_ASSERT_EQUAL_UINT8(12, kBaseTier.rows);
    TEST_ASSERT_EQUAL_UINT8(12, kBaseTier.maxWidgetsPerPage);
}

void test_everyShippedPanelResolvesToBase() {
    TEST_ASSERT_EQUAL_STRING("base", tierForPanel(320, 240).id);
}

void test_aPanelSmallerThanBaseStillGetsBase() {
    TEST_ASSERT_EQUAL_STRING("base", tierForPanel(240, 320).id);
    TEST_ASSERT_EQUAL_STRING("base", tierForPanel(128, 64).id);
}

void test_aLargerPanelTakesTheLargestTierItFits() {
    TEST_ASSERT_EQUAL_STRING("medium", tierForPanel(480, 320).id);
    TEST_ASSERT_EQUAL_STRING("large", tierForPanel(800, 480).id);
    TEST_ASSERT_EQUAL_STRING("large", tierForPanel(1024, 600).id);
}

void test_aPanelBetweenTwoTiersNeverRoundsUp() {
    TEST_ASSERT_EQUAL_STRING("base", tierForPanel(479, 319).id);
    TEST_ASSERT_EQUAL_STRING("medium", tierForPanel(799, 479).id);
}

void test_aTallPanelIsNotAWideOne() {
    TEST_ASSERT_EQUAL_STRING("base", tierForPanel(480, 240).id);
    TEST_ASSERT_EQUAL_STRING("base", tierForPanel(320, 480).id);
}

void test_everyTierGrowsItsGridAndItsWidgetBudget() {
    for (size_t i = 1; i < kDisplayTierCount; ++i) {
        const DisplayTier &smaller = kDisplayTiers[i - 1];
        const DisplayTier &bigger = kDisplayTiers[i];
        TEST_ASSERT_TRUE(tierDesignArea(bigger) > tierDesignArea(smaller));
        TEST_ASSERT_TRUE(bigger.columns > smaller.columns);
        TEST_ASSERT_TRUE(bigger.rows > smaller.rows);
        TEST_ASSERT_TRUE(bigger.maxWidgetsPerPage > smaller.maxWidgetsPerPage);
    }
}

void test_everyLadderRisesAndTheBiggerTierStartsHigher() {
    for (size_t i = 0; i < kDisplayTierCount; ++i) {
        const FaceLadder &values = kDisplayTiers[i].valueFaces;
        const FaceLadder &labels = kDisplayTiers[i].labelFaces;
        TEST_ASSERT_TRUE(values.count > 0);
        TEST_ASSERT_TRUE(labels.count > 0);
        for (size_t f = 1; f < values.count; ++f)
            TEST_ASSERT_TRUE(values.sizes[f] > values.sizes[f - 1]);
        for (size_t f = 1; f < labels.count; ++f)
            TEST_ASSERT_TRUE(labels.sizes[f] > labels.sizes[f - 1]);
        if (i == 0)
            continue;
        TEST_ASSERT_TRUE(values.sizes[values.count - 1] >
                         kDisplayTiers[i - 1].valueFaces.sizes[values.count - 1]);
    }
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_baseTierIsTheDesignSpaceEveryPageIsAuthoredIn);
    RUN_TEST(test_everyShippedPanelResolvesToBase);
    RUN_TEST(test_aPanelSmallerThanBaseStillGetsBase);
    RUN_TEST(test_aLargerPanelTakesTheLargestTierItFits);
    RUN_TEST(test_aPanelBetweenTwoTiersNeverRoundsUp);
    RUN_TEST(test_aTallPanelIsNotAWideOne);
    RUN_TEST(test_everyTierGrowsItsGridAndItsWidgetBudget);
    RUN_TEST(test_everyLadderRisesAndTheBiggerTierStartsHigher);
    return UNITY_END();
}
