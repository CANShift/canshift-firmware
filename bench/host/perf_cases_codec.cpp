#include "perf_cases.h"

#include "can/can_parser.h"
#include "can_parser_rs.h"
#include "config/config_loader.h"
#include "hal/ble/telemetry_frame.h"
#include "hal/usb/usb_status_line.h"
#include "util/format_float.h"

#include <cstring>
#include <iterator>

namespace {

constexpr size_t kMaxFrames = 32;
constexpr uint8_t kFrameBytes = 8;
constexpr uint32_t kLcgMultiplier = 1664525u;
constexpr uint32_t kLcgIncrement = 1013904223u;
constexpr uint32_t kCalibrationSteps = 64;

struct BenchFrame {
    uint32_t id;
    uint8_t data[kFrameBytes];
};

BenchFrame s_frames[kMaxFrames];
size_t s_frameCount = 0;

constexpr const char *kExpressions[] = {"V*0.1-40", "(B0<<8|B1)/10", "Round(V/3.6)", "V>100",
                                        "(V&0x0F)*2+1"};
constexpr size_t kExpressionCount = std::size(kExpressions);

struct CompiledExpr {
    FfiTok tokens[kCanshiftExprMaxTokens];
    size_t count;
};

CompiledExpr s_compiled[kExpressionCount];

constexpr uint8_t kExprPayload[kFrameBytes] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0};

constexpr float kFloatValues[] = {12.3456f, -40.0f, 0.0f, 7999.5f, 0.001f, 123456.0f};
constexpr size_t kFloatValueCount = std::size(kFloatValues);
constexpr int kMaxDecimals = 3;

uint32_t nextLcg(uint32_t state) {
    return state * kLcgMultiplier + kLcgIncrement;
}

bool knownFrame(uint32_t id) {
    for (size_t i = 0; i < s_frameCount; ++i) {
        if (s_frames[i].id == id)
            return true;
    }
    return false;
}

void fillPayload(BenchFrame &frame, uint32_t seed) {
    for (uint8_t &byte : frame.data) {
        seed = nextLcg(seed);
        byte = static_cast<uint8_t>(seed >> 24);
    }
}

void addFrame(uint32_t id) {
    if (s_frameCount >= kMaxFrames || knownFrame(id))
        return;
    BenchFrame &frame = s_frames[s_frameCount++];
    frame.id = id;
    fillPayload(frame, id);
}

uint64_t floatBits(float value) {
    uint32_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

} // namespace

namespace PerfCases {

bool setupNothing(const char *) {
    return true;
}

void runCalibration(uint32_t index) {
    uint32_t state = index;
    for (uint32_t i = 0; i < kCalibrationSteps; ++i)
        state = nextLcg(state) ^ (state >> 7);
    PerfTimer::keep(state);
}

bool setupCanDecode(const char *) {
    const CfgSignalConfig &cfg = ConfigLoader::getSignalConfig();
    s_frameCount = 0;
    for (uint8_t i = 0; i < cfg.signalCount; ++i)
        addFrame(cfg.signals[i].canFrameId);
    return s_frameCount > 0;
}

void runCanDecode(uint32_t index) {
    const BenchFrame &frame = s_frames[index % s_frameCount];
    CanParser::parseFrame(frame.id, frame.data, kFrameBytes);
}

bool setupExprEval(const char *) {
    for (size_t i = 0; i < kExpressionCount; ++i) {
        const char *expr = kExpressions[i];
        const int32_t n = compile_expr_rs(reinterpret_cast<const uint8_t *>(expr), strlen(expr),
                                          s_compiled[i].tokens, kCanshiftExprMaxTokens);
        if (n <= 0)
            return false;
        s_compiled[i].count = static_cast<size_t>(n);
    }
    return true;
}

void runExprEval(uint32_t index) {
    const CompiledExpr &expr = s_compiled[index % kExpressionCount];
    const float value = eval_tokens_rs(kExprPayload, 0, 2, false, false, 0, 1.0f, 0.0f, kFrameBytes,
                                       expr.tokens, expr.count);
    PerfTimer::keep(floatBits(value));
}

void runTelemetryEncode(uint32_t index) {
    TelemetryFrame::Field fields[TelemetryFrame::kMaxFields];
    for (size_t i = 0; i < TelemetryFrame::kMaxFields; ++i)
        fields[i] = {(index + i) % 5 != 0, static_cast<float>(index + i) * 0.37f};
    uint8_t buf[TelemetryFrame::kMaxFrameBytes];
    PerfTimer::keep(TelemetryFrame::encode(fields, TelemetryFrame::kMaxFields, buf, sizeof(buf)));
}

void runStatusWriter(uint32_t index) {
    const UsbStatusLine::Fields fields = {"0.17.0", 3u, (index & 1u) != 0, "crowpanel_28"};
    char out[160];
    PerfTimer::keep(UsbStatusLine::format(fields, out, sizeof(out)));
}

void runFormatFloat(uint32_t index) {
    char buf[24];
    const float value = kFloatValues[index % kFloatValueCount];
    const int decimals = static_cast<int>(index % (kMaxDecimals + 1));
    PerfTimer::keep(FloatFormat::formatFixed(buf, sizeof(buf), value, decimals));
}

} // namespace PerfCases
