// sensor_color_ramp.cpp — implementation. See header for contract.

#include "sensor_color_ramp.h"

#include <cmath>
#include <ctype.h>
#include <stddef.h>
#include <string.h>

#if USE_RUST_SENSOR_COLOR_RAMP
    #include "sensor_color_ramp_rs.h"
#endif

namespace {

// Channel-wise lerp between two RGB ints. `t` clamped to [0,1] by the caller.
uint32_t lerpRgb(uint32_t a, uint32_t b, float t) {
    if (t < 0.0f)
        t = 0.0f;
    if (t > 1.0f)
        t = 1.0f;
    auto channel = [](uint32_t c, int shift) -> int {
        return static_cast<int>((c >> shift) & 0xFFu);
    };
    const int ar = channel(a, 16);
    const int ag = channel(a, 8);
    const int ab = channel(a, 0);
    const int br = channel(b, 16);
    const int bg = channel(b, 8);
    const int bb = channel(b, 0);
    auto mix = [t](int x, int y) -> uint32_t {
        const float v = static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * t;
        if (v < 0.0f)
            return 0u;
        if (v > 255.0f)
            return 255u;
        // Use std::lround to avoid the bias of (int)(v + 0.5f) on negative
        // values + the rounding-toward-zero bug clang-tidy flagged
        // (bugprone-incorrect-roundings). Input clamped above so cast is safe.
        return static_cast<uint32_t>(std::lround(v));
    };
    return (mix(ar, br) << 16) | (mix(ag, bg) << 8) | mix(ab, bb);
}

// Lower-cased strstr — case-insensitive substring search.
bool containsCi(const char *haystack, const char *needle) {
    if (!haystack || !needle)
        return false;
    const size_t hLen = strlen(haystack);
    const size_t nLen = strlen(needle);
    if (nLen == 0)
        return true;
    if (nLen > hLen)
        return false;
    for (size_t i = 0; i + nLen <= hLen; ++i) {
        size_t j = 0;
        for (; j < nLen; ++j) {
            const char hc = static_cast<char>(tolower(static_cast<unsigned char>(haystack[i + j])));
            const char nc = static_cast<char>(tolower(static_cast<unsigned char>(needle[j])));
            if (hc != nc)
                break;
        }
        if (j == nLen)
            return true;
    }
    return false;
}

struct NameRule {
    const char *pattern;
    SensorKind kind;
};

// Mirror of NAME_HEURISTICS in canshift-core/src/sensorDefaults.ts. Order
// matters — more specific patterns must come first.
constexpr NameRule kNameRules[] = {
    {"coolant", SensorKind::Coolant},
    {"oil_press", SensorKind::OilPress},
    {"oil_pressure", SensorKind::OilPress},
    {"oil_temp", SensorKind::OilTemp},
    {"oil", SensorKind::OilTemp},
    {"battery", SensorKind::BatteryVolts},
    {"batt_v", SensorKind::BatteryVolts},
    {"rpm", SensorKind::Rpm},
    {"afr", SensorKind::Afr},
    {"lambda", SensorKind::Afr},
    {"boost", SensorKind::Boost},
    {"manifold_press", SensorKind::Boost},
    {"map_press", SensorKind::Boost},
    {"intake_temp", SensorKind::IntakeTemp},
    {"iat", SensorKind::IntakeTemp},
    {"mat", SensorKind::IntakeTemp},
    {"egt", SensorKind::Egt},
    {"exhaust_temp", SensorKind::Egt},
};

// Helper to assemble a stop array literal at compile time. The unused tail of
// `stops` is zero-initialised by the implicit aggregate rules.
constexpr CfgRampStop S(float v, uint32_t c) {
    return CfgRampStop{v, c};
}

} // namespace

// Default catalog — index aligns with SensorKind. Hex literals chosen to match
// SENSOR_DEFAULT_RAMPS in canshift-core/src/sensorDefaults.ts.
//
// CfgColorRamp / CfgRampStop are POD aggregates; the initializer is
// non-throwing in practice. clang-tidy can't prove noexcept on the implicit
// ctor chain because the structs don't declare `noexcept` explicitly.
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
const CfgColorRamp kSensorDefaultRamps[kSensorKindCount] = {
    // Coolant
    {4,
     CfgRampInterp::Linear,
     {S(60.0f, 0x4A90E2u), S(90.0f, 0x44CC66u), S(100.0f, 0xCC8800u), S(110.0f, 0xCC3333u)}},
    // OilTemp
    {4,
     CfgRampInterp::Linear,
     {S(70.0f, 0x4A90E2u), S(95.0f, 0x44CC66u), S(120.0f, 0xCC8800u), S(135.0f, 0xCC3333u)}},
    // OilPress
    {4,
     CfgRampInterp::Linear,
     {S(1.0f, 0xCC3333u), S(1.8f, 0xCC8800u), S(2.5f, 0x44CC66u), S(6.0f, 0x44CC66u)}},
    // BatteryVolts
    {5,
     CfgRampInterp::Linear,
     {S(11.5f, 0xCC3333u), S(12.5f, 0xCC8800u), S(13.5f, 0x44CC66u), S(14.8f, 0xCC8800u),
      S(15.5f, 0xCC3333u)}},
    // Rpm
    {4,
     CfgRampInterp::Linear,
     {S(1500.0f, 0x44CC66u), S(5500.0f, 0x44CC66u), S(6500.0f, 0xCC8800u), S(7000.0f, 0xCC3333u)}},
    // Afr
    {5,
     CfgRampInterp::Linear,
     {S(10.5f, 0xCC3333u), S(11.8f, 0xCC8800u), S(13.0f, 0x44CC66u), S(14.7f, 0x44CC66u),
      S(16.0f, 0xCC8800u)}},
    // Boost
    {4,
     CfgRampInterp::Linear,
     {S(0.0f, 0x44CC66u), S(1.0f, 0x44CC66u), S(1.4f, 0xCC8800u), S(1.7f, 0xCC3333u)}},
    // IntakeTemp
    {3, CfgRampInterp::Linear, {S(20.0f, 0x44CC66u), S(50.0f, 0xCC8800u), S(65.0f, 0xCC3333u)}},
    // Egt
    {3, CfgRampInterp::Linear, {S(600.0f, 0x44CC66u), S(850.0f, 0xCC8800u), S(950.0f, 0xCC3333u)}},
};

SensorKind sensorKindFromName(const char *signalName) {
#if USE_RUST_SENSOR_COLOR_RAMP
    return static_cast<SensorKind>(sensor_kind_from_name_rs(signalName));
#else
    if (!signalName || signalName[0] == '\0')
        return SensorKind::Unknown;
    for (const NameRule &rule : kNameRules) {
        if (containsCi(signalName, rule.pattern))
            return rule.kind;
    }
    return SensorKind::Unknown;
#endif
}

const CfgColorRamp *resolveRamp(const CfgColorRamp &perSignal, const char *signalName) {
#if USE_RUST_SENSOR_COLOR_RAMP
    return resolve_ramp_rs(&perSignal, signalName);
#else
    if (perSignal.count > 0)
        return &perSignal;
    const SensorKind kind = sensorKindFromName(signalName);
    if (kind == SensorKind::Unknown)
        return nullptr;
    return &kSensorDefaultRamps[static_cast<uint8_t>(kind)];
#endif
}

uint32_t colorAtValue(const CfgColorRamp &ramp, float value) {
#if USE_RUST_SENSOR_COLOR_RAMP
    return color_at_value_rs(&ramp, value);
#else
    if (ramp.count == 0)
        return 0x000000u;
    const CfgRampStop &first = ramp.stops[0];
    const CfgRampStop &last = ramp.stops[ramp.count - 1];
    if (ramp.count == 1 || value <= first.value)
        return first.color;
    if (value >= last.value)
        return last.color;

    // Step mode: walk forward, the highest stop with value <= input wins.
    if (ramp.interpolate == CfgRampInterp::Step) {
        uint32_t active = first.color;
        for (uint8_t i = 0; i < ramp.count; ++i) {
            if (value >= ramp.stops[i].value)
                active = ramp.stops[i].color;
            else
                break;
        }
        return active;
    }

    // Linear: find bracketing pair, lerp.
    for (uint8_t i = 0; i + 1 < ramp.count; ++i) {
        const CfgRampStop &lower = ramp.stops[i];
        const CfgRampStop &upper = ramp.stops[i + 1];
        if (value >= lower.value && value <= upper.value) {
            const float span = upper.value - lower.value;
            const float t = span > 0.0f ? (value - lower.value) / span : 0.0f;
            return lerpRgb(lower.color, upper.color, t);
        }
    }

    return last.color;
#endif
}
