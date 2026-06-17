// test_main.cpp — Unity tests for the firmware-side color ramp lookup.
//
// Two responsibilities:
//   1. Behaviour — `colorAtValue` and `sensorKindFromName` mirror the TS
//      reference semantics exactly.
//   2. Parity — `kSensorDefaultRamps` matches the JSON fixture exported from
//      canshift-core's SENSOR_DEFAULT_RAMPS table. Drift between the two
//      sides of the wire fails CI here.

#include "ui/sensor_color_ramp.h"

#include <ArduinoJson.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "fixtures/sensor_defaults.json.h"

namespace {

constexpr float kFloatEps = 1e-4f;

const char *kindToSignalName(SensorKind kind) {
    switch (kind) {
        case SensorKind::Coolant:      return "coolant_temp_c";
        case SensorKind::OilTemp:      return "oil_temp_c";
        case SensorKind::OilPress:     return "oil_press_bar";
        case SensorKind::BatteryVolts: return "battery_volts";
        case SensorKind::Rpm:          return "rpm";
        case SensorKind::Afr:          return "afr_1";
        case SensorKind::Boost:        return "boost_bar";
        case SensorKind::IntakeTemp:   return "intake_temp_c";
        case SensorKind::Egt:          return "egt_c";
        default:                       return "";
    }
}

const CfgColorRamp &rampFor(SensorKind kind) {
    static const CfgColorRamp kEmpty = {};
    return *resolveRamp(kEmpty, kindToSignalName(kind));
}

bool stringMatches(const char *kindStr, SensorKind expected) {
    if (strcmp(kindStr, "coolant_temp") == 0) return expected == SensorKind::Coolant;
    if (strcmp(kindStr, "oil_temp") == 0) return expected == SensorKind::OilTemp;
    if (strcmp(kindStr, "oil_press") == 0) return expected == SensorKind::OilPress;
    if (strcmp(kindStr, "battery_volts") == 0) return expected == SensorKind::BatteryVolts;
    if (strcmp(kindStr, "rpm") == 0) return expected == SensorKind::Rpm;
    if (strcmp(kindStr, "afr") == 0) return expected == SensorKind::Afr;
    if (strcmp(kindStr, "boost") == 0) return expected == SensorKind::Boost;
    if (strcmp(kindStr, "intake_temp") == 0) return expected == SensorKind::IntakeTemp;
    if (strcmp(kindStr, "egt") == 0) return expected == SensorKind::Egt;
    return false;
}

SensorKind kindFromString(const char *kindStr) {
    if (strcmp(kindStr, "coolant_temp") == 0) return SensorKind::Coolant;
    if (strcmp(kindStr, "oil_temp") == 0) return SensorKind::OilTemp;
    if (strcmp(kindStr, "oil_press") == 0) return SensorKind::OilPress;
    if (strcmp(kindStr, "battery_volts") == 0) return SensorKind::BatteryVolts;
    if (strcmp(kindStr, "rpm") == 0) return SensorKind::Rpm;
    if (strcmp(kindStr, "afr") == 0) return SensorKind::Afr;
    if (strcmp(kindStr, "boost") == 0) return SensorKind::Boost;
    if (strcmp(kindStr, "intake_temp") == 0) return SensorKind::IntakeTemp;
    if (strcmp(kindStr, "egt") == 0) return SensorKind::Egt;
    return SensorKind::Unknown;
}

uint32_t parseHexLiteral(const char *hex) {
    if (!hex || hex[0] != '#') return 0u;
    return static_cast<uint32_t>(strtoul(hex + 1, nullptr, 16));
}

} // namespace

void setUp() {}
void tearDown() {}

void test_colorAtValue_belowFirstStop_returnsFirstColor() {
    const CfgColorRamp &ramp = rampFor(SensorKind::Coolant);
    TEST_ASSERT_EQUAL_HEX32(ramp.stops[0].color, colorAtValue(ramp, 30.0f));
}

void test_colorAtValue_aboveLastStop_returnsLastColor() {
    const CfgColorRamp &ramp = rampFor(SensorKind::Coolant);
    TEST_ASSERT_EQUAL_HEX32(ramp.stops[ramp.count - 1].color, colorAtValue(ramp, 250.0f));
}

void test_colorAtValue_exactStop_returnsStopColor() {
    const CfgColorRamp &ramp = rampFor(SensorKind::Coolant);
    TEST_ASSERT_EQUAL_HEX32(0x44CC66u, colorAtValue(ramp, 90.0f));
}

void test_colorAtValue_linearLerpMidSegment() {
    // Mid between coolant 90 (#44CC66) and 100 (#CC8800) at 95.
    // Expected: R=(0x44+0xCC)/2=0x88, G=(0xCC+0x88)/2=0xAA, B=(0x66+0)/2=0x33
    const CfgColorRamp &ramp = rampFor(SensorKind::Coolant);
    const uint32_t got = colorAtValue(ramp, 95.0f);
    // Allow ±1 per channel for the +0.5 rounding inside lerpRgb.
    const uint8_t r = (got >> 16) & 0xFFu;
    const uint8_t g = (got >> 8) & 0xFFu;
    const uint8_t b = got & 0xFFu;
    TEST_ASSERT_INT_WITHIN(1, 0x88, r);
    TEST_ASSERT_INT_WITHIN(1, 0xAA, g);
    TEST_ASSERT_INT_WITHIN(1, 0x33, b);
}

void test_colorAtValue_step_returnsLowerColorBetweenStops() {
    CfgColorRamp ramp{};
    ramp.count = 3;
    ramp.interpolate = CfgRampInterp::Step;
    ramp.stops[0] = {0.0f, 0x44CC66u};
    ramp.stops[1] = {50.0f, 0xCC8800u};
    ramp.stops[2] = {100.0f, 0xCC3333u};
    TEST_ASSERT_EQUAL_HEX32(0x44CC66u, colorAtValue(ramp, 25.0f));
    TEST_ASSERT_EQUAL_HEX32(0xCC8800u, colorAtValue(ramp, 50.0f));
    TEST_ASSERT_EQUAL_HEX32(0xCC8800u, colorAtValue(ramp, 75.0f));
    TEST_ASSERT_EQUAL_HEX32(0xCC3333u, colorAtValue(ramp, 100.0f));
}

void test_sensorKindFromName_matchesCommonAliases() {
    TEST_ASSERT_TRUE(sensorKindFromName("coolant_temp_c") == SensorKind::Coolant);
    TEST_ASSERT_TRUE(sensorKindFromName("oil_pressure_bar") == SensorKind::OilPress);
    TEST_ASSERT_TRUE(sensorKindFromName("oil_temp_c") == SensorKind::OilTemp);
    TEST_ASSERT_TRUE(sensorKindFromName("battery_volts") == SensorKind::BatteryVolts);
    TEST_ASSERT_TRUE(sensorKindFromName("rpm") == SensorKind::Rpm);
    TEST_ASSERT_TRUE(sensorKindFromName("afr") == SensorKind::Afr);
    TEST_ASSERT_TRUE(sensorKindFromName("lambda") == SensorKind::Afr);
    TEST_ASSERT_TRUE(sensorKindFromName("boost_bar") == SensorKind::Boost);
    TEST_ASSERT_TRUE(sensorKindFromName("iat") == SensorKind::IntakeTemp);
    TEST_ASSERT_TRUE(sensorKindFromName("egt_c") == SensorKind::Egt);
}

void test_sensorKindFromName_unknownReturnsUnknown() {
    TEST_ASSERT_TRUE(sensorKindFromName("throttle_pos_pct") == SensorKind::Unknown);
    TEST_ASSERT_TRUE(sensorKindFromName("") == SensorKind::Unknown);
    TEST_ASSERT_TRUE(sensorKindFromName(nullptr) == SensorKind::Unknown);
}

void test_sensorKindFromName_isCaseInsensitive() {
    TEST_ASSERT_TRUE(sensorKindFromName("COOLANT_TEMP_C") == SensorKind::Coolant);
}

void test_kSensorDefaultRamps_matchesCoreFixture() {
    JsonDocument doc;
    DeserializationError err = deserializeJson(
        doc, fixtures::kSensorDefaultsJson, strlen(fixtures::kSensorDefaultsJson));
    TEST_ASSERT_FALSE_MESSAGE(err, "fixture JSON failed to parse");

    JsonObjectConst root = doc.as<JsonObjectConst>();
    TEST_ASSERT_TRUE(!root.isNull());

    uint8_t covered = 0;
    for (JsonPairConst entry : root) {
        const char *kindStr = entry.key().c_str();
        SensorKind kind = kindFromString(kindStr);
        TEST_ASSERT_FALSE_MESSAGE(kind == SensorKind::Unknown, kindStr);

        JsonObjectConst rampJson = entry.value().as<JsonObjectConst>();
        const char *interp = rampJson["interpolate"] | "linear";
        const CfgColorRamp &got = rampFor(kind);
        const CfgRampInterp expectedInterp =
            (strcmp(interp, "step") == 0) ? CfgRampInterp::Step : CfgRampInterp::Linear;
        TEST_ASSERT_TRUE_MESSAGE(expectedInterp == got.interpolate, kindStr);

        JsonArrayConst stopsJson = rampJson["stops"];
        const size_t expectedCount = stopsJson.size();
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(static_cast<uint8_t>(expectedCount), got.count, kindStr);

        size_t i = 0;
        for (JsonObjectConst s : stopsJson) {
            const float expectedValue = s["value"] | 0.0f;
            const char *expectedColorHex = s["color"] | "#000000";
            const uint32_t expectedColor = parseHexLiteral(expectedColorHex);
            TEST_ASSERT_FLOAT_WITHIN(kFloatEps, expectedValue, got.stops[i].value);
            TEST_ASSERT_EQUAL_HEX32(expectedColor, got.stops[i].color);
            ++i;
        }

        TEST_ASSERT_TRUE_MESSAGE(stringMatches(kindStr, kind), kindStr);
        ++covered;
    }

    // Every named SensorKind appeared in the fixture.
    TEST_ASSERT_EQUAL_UINT8(kSensorKindCount, covered);
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_colorAtValue_belowFirstStop_returnsFirstColor);
    RUN_TEST(test_colorAtValue_aboveLastStop_returnsLastColor);
    RUN_TEST(test_colorAtValue_exactStop_returnsStopColor);
    RUN_TEST(test_colorAtValue_linearLerpMidSegment);
    RUN_TEST(test_colorAtValue_step_returnsLowerColorBetweenStops);
    RUN_TEST(test_sensorKindFromName_matchesCommonAliases);
    RUN_TEST(test_sensorKindFromName_unknownReturnsUnknown);
    RUN_TEST(test_sensorKindFromName_isCaseInsensitive);
    RUN_TEST(test_kSensorDefaultRamps_matchesCoreFixture);
    return UNITY_END();
}
