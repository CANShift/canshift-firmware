#include "ui/signal_presentation.h"

#include <stdio.h>
#include <string.h>
#include <unity.h>

void setUp() {}
void tearDown() {}

static void assertKickerUniqueAfter(const SignalPresentation::Entry *entries, size_t count,
                                    size_t i) {
    for (size_t j = i + 1; j < count; ++j) {
        if (strcmp(entries[i].kicker, entries[j].kicker) != 0)
            continue;
        char message[128];
        snprintf(message, sizeof(message), "'%s' and '%s' both render '%s'", entries[i].signalId,
                 entries[j].signalId, entries[i].kicker);
        TEST_FAIL_MESSAGE(message);
    }
}

void test_no_two_signals_share_a_kicker() {
    const SignalPresentation::Entry *entries = SignalPresentation::entries();
    const size_t count = SignalPresentation::entryCount();
    for (size_t i = 0; i < count; ++i)
        assertKickerUniqueAfter(entries, count, i);
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

static void assertFallback(const char *signalId, const char *expected) {
    char out[32];
    SignalPresentation::fallbackKicker(signalId, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING(expected, out);
}

void test_obd_signals_have_curated_kickers() {
    TEST_ASSERT_EQUAL_STRING("MAF", SignalPresentation::kickerForSignal("maf_gps"));
    TEST_ASSERT_EQUAL_STRING("RUN TIME", SignalPresentation::kickerForSignal("run_time_s"));
}

void test_fallback_drops_a_trailing_unit() {
    assertFallback("brake_temp_c", "BRAKE TEMP");
    assertFallback("lap_time_ms", "LAP TIME");
    assertFallback("wheel_speed_fl_kph", "WHEEL SPEED FL");
    assertFallback("baro_kpa_", "BARO");
}

void test_fallback_keeps_names_that_only_look_like_units() {
    assertFallback("brake_temp_fl", "BRAKE TEMP FL");
    assertFallback("__lap__time_", "LAP TIME");
    assertFallback("bar", "BAR");
    assertFallback("_s_", "S");
    assertFallback("s_count", "S COUNT");
    assertFallback(nullptr, "");
}

void test_fallback_never_overruns_the_buffer() {
    char out[6];
    SignalPresentation::fallbackKicker("brake_temp_fl", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("BRAKE", out);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_no_two_signals_share_a_kicker);
    RUN_TEST(test_oil_pair_reads_apart);
    RUN_TEST(test_unknown_signal_has_no_kicker_and_no_unit);
    RUN_TEST(test_obd_signals_have_curated_kickers);
    RUN_TEST(test_fallback_drops_a_trailing_unit);
    RUN_TEST(test_fallback_keeps_names_that_only_look_like_units);
    RUN_TEST(test_fallback_never_overruns_the_buffer);
    return UNITY_END();
}
