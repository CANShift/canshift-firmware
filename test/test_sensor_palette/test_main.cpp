// test_main.cpp — Unity tests for the semantic two-zone sensor palette
// (issue #954). Lock the lookup / fillColor contract and the few entries
// that are most likely to be relied on by the studio + firmware.

#include "ui/sensor_palette.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

namespace {

void test_lookup_unknownIsNull() {
    TEST_ASSERT_NULL(SensorPalette::lookup(nullptr));
    TEST_ASSERT_NULL(SensorPalette::lookup(""));
    TEST_ASSERT_NULL(SensorPalette::lookup("not_a_real_sensor"));
}

void test_lookup_knownSensor() {
    const SensorPaletteEntry *coolant = SensorPalette::lookup("coolant");
    TEST_ASSERT_NOT_NULL(coolant);
    TEST_ASSERT_EQUAL_HEX32(0x1E88E5u, coolant->okColor);
    TEST_ASSERT_EQUAL_HEX32(0xCC3333u, coolant->warningColor);

    const SensorPaletteEntry *throttle = SensorPalette::lookup("throttle");
    TEST_ASSERT_NOT_NULL(throttle);
    TEST_ASSERT_EQUAL_HEX32(0xFB8C00u, throttle->okColor);
    TEST_ASSERT_EQUAL_HEX32(SensorPalette::kSentinelNoWarning, throttle->warningColor);
}

void test_fillColor_belowWarningStaysOnOkColor() {
    TEST_ASSERT_EQUAL_HEX32(0x1E88E5u, SensorPalette::fillColor("coolant", 80.0f, 100.0f));
}

void test_fillColor_atOrAboveWarningSwitchesToWarning() {
    TEST_ASSERT_EQUAL_HEX32(0xCC3333u, SensorPalette::fillColor("coolant", 100.0f, 100.0f));
    TEST_ASSERT_EQUAL_HEX32(0xCC3333u, SensorPalette::fillColor("coolant", 110.0f, 100.0f));
}

void test_fillColor_noWarningSensorStaysOkAcrossRange() {
    TEST_ASSERT_EQUAL_HEX32(0xFB8C00u, SensorPalette::fillColor("throttle", 0.0f, 100.0f));
    TEST_ASSERT_EQUAL_HEX32(0xFB8C00u, SensorPalette::fillColor("throttle", 100.0f, 100.0f));
}

void test_fillColor_nanWarningKeepsOkColor() {
    TEST_ASSERT_EQUAL_HEX32(0x1E88E5u, SensorPalette::fillColor("coolant", 200.0f, NAN));
}

void test_fillColor_unknownReturnsZero() {
    TEST_ASSERT_EQUAL_HEX32(0u, SensorPalette::fillColor(nullptr, 0.0f, 0.0f));
    TEST_ASSERT_EQUAL_HEX32(0u, SensorPalette::fillColor("", 0.0f, 0.0f));
    TEST_ASSERT_EQUAL_HEX32(0u, SensorPalette::fillColor("nope", 0.0f, 0.0f));
}

} // namespace

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_lookup_unknownIsNull);
    RUN_TEST(test_lookup_knownSensor);
    RUN_TEST(test_fillColor_belowWarningStaysOnOkColor);
    RUN_TEST(test_fillColor_atOrAboveWarningSwitchesToWarning);
    RUN_TEST(test_fillColor_noWarningSensorStaysOkAcrossRange);
    RUN_TEST(test_fillColor_nanWarningKeepsOkColor);
    RUN_TEST(test_fillColor_unknownReturnsZero);
    return UNITY_END();
}
