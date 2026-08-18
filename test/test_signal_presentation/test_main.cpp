#include "ui/signal_presentation.h"

#include <stdio.h>
#include <string.h>
#include <unity.h>

void setUp() {}
void tearDown() {}

void test_no_two_signals_share_a_kicker() {
    const SignalPresentation::Entry *entries = SignalPresentation::entries();
    const size_t count = SignalPresentation::entryCount();
    for (size_t i = 0; i < count; ++i) {
        for (size_t j = i + 1; j < count; ++j) {
            if (strcmp(entries[i].kicker, entries[j].kicker) != 0)
                continue;
            char message[128];
            snprintf(message, sizeof(message), "'%s' and '%s' both render '%s'",
                     entries[i].signalId, entries[j].signalId, entries[i].kicker);
            TEST_FAIL_MESSAGE(message);
        }
    }
}

void test_oil_pair_reads_apart() {
    TEST_ASSERT_EQUAL_STRING("OIL T", SignalPresentation::kickerForSignal("oil_temp_c"));
    TEST_ASSERT_EQUAL_STRING("TARGET", SignalPresentation::kickerForSignal("boost_target_bar"));
    TEST_ASSERT_EQUAL_STRING("OIL PRESS", SignalPresentation::kickerForSignal("oil_press_bar"));
    TEST_ASSERT_EQUAL_STRING("°C", SignalPresentation::unitForSignal("oil_temp_c"));
    TEST_ASSERT_EQUAL_STRING("bar", SignalPresentation::unitForSignal("oil_press_bar"));
}

void test_unknown_signal_has_no_kicker_and_no_unit() {
    TEST_ASSERT_NULL(SignalPresentation::kickerForSignal("not_a_signal"));
    TEST_ASSERT_NULL(SignalPresentation::kickerForSignal(""));
    TEST_ASSERT_NULL(SignalPresentation::kickerForSignal(nullptr));
    TEST_ASSERT_EQUAL_STRING("", SignalPresentation::unitForSignal("not_a_signal"));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_no_two_signals_share_a_kicker);
    RUN_TEST(test_oil_pair_reads_apart);
    RUN_TEST(test_unknown_signal_has_no_kicker_and_no_unit);
    return UNITY_END();
}
