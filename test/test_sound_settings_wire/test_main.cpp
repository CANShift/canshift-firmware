#include "runtime/sound_settings_wire.h"

#include <ArduinoJson.h>
#include <unity.h>

namespace {

using SoundSettingsWire::Merge;
using SoundSettingsWire::Values;

constexpr Values kStart = {70, true, false};

Merge mergeJson(const char *json, Values &values) {
    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, json) == DeserializationError::Ok);
    return SoundSettingsWire::merge(doc.as<JsonObjectConst>(), values);
}

void assertValues(const Values &expected, const Values &actual) {
    TEST_ASSERT_EQUAL_UINT8(expected.volumePct, actual.volumePct);
    TEST_ASSERT_EQUAL(expected.boot, actual.boot);
    TEST_ASSERT_EQUAL(expected.shiftTone, actual.shiftTone);
}

}

void setUp() {}
void tearDown() {}

void test_brightnessOnlyPayloadLeavesSoundAlone() {
    Values values = kStart;
    TEST_ASSERT_EQUAL(static_cast<int>(Merge::Absent),
                      static_cast<int>(mergeJson("{\"brightness\":80}", values)));
    assertValues(kStart, values);
}

void test_everyFieldIsApplied() {
    Values values = kStart;
    TEST_ASSERT_EQUAL(
        static_cast<int>(Merge::Changed),
        static_cast<int>(mergeJson(
            "{\"brightness\":80,\"volume\":0,\"sounds\":{\"boot\":false,\"shift_tone\":true}}",
            values)));
    assertValues({0, false, true}, values);
}

void test_partialSoundsKeepTheOtherFlag() {
    Values values = kStart;
    TEST_ASSERT_EQUAL(static_cast<int>(Merge::Changed),
                      static_cast<int>(mergeJson("{\"sounds\":{\"shift_tone\":true}}", values)));
    assertValues({70, true, true}, values);
}

void test_outOfRangeVolumeRejectsTheWholeUpdate() {
    Values values = kStart;
    TEST_ASSERT_EQUAL(
        static_cast<int>(Merge::Invalid),
        static_cast<int>(mergeJson("{\"volume\":101,\"sounds\":{\"boot\":false}}", values)));
    assertValues(kStart, values);
    TEST_ASSERT_EQUAL(static_cast<int>(Merge::Invalid),
                      static_cast<int>(mergeJson("{\"volume\":-1}", values)));
}

void test_wrongTypesAreRejected() {
    Values values = kStart;
    TEST_ASSERT_EQUAL(static_cast<int>(Merge::Invalid),
                      static_cast<int>(mergeJson("{\"volume\":\"loud\"}", values)));
    TEST_ASSERT_EQUAL(static_cast<int>(Merge::Invalid),
                      static_cast<int>(mergeJson("{\"sounds\":{\"boot\":1}}", values)));
    TEST_ASSERT_EQUAL(static_cast<int>(Merge::Invalid),
                      static_cast<int>(mergeJson("{\"sounds\":true}", values)));
    assertValues(kStart, values);
}

void test_writeRoundTripsThroughMerge() {
    JsonDocument doc;
    SoundSettingsWire::write(doc.to<JsonObject>(), {25, false, true});
    Values values = kStart;
    TEST_ASSERT_EQUAL(static_cast<int>(Merge::Changed), static_cast<int>(SoundSettingsWire::merge(
                                                            doc.as<JsonObjectConst>(), values)));
    assertValues({25, false, true}, values);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_brightnessOnlyPayloadLeavesSoundAlone);
    RUN_TEST(test_everyFieldIsApplied);
    RUN_TEST(test_partialSoundsKeepTheOtherFlag);
    RUN_TEST(test_outOfRangeVolumeRejectsTheWholeUpdate);
    RUN_TEST(test_wrongTypesAreRejected);
    RUN_TEST(test_writeRoundTripsThroughMerge);
    return UNITY_END();
}
