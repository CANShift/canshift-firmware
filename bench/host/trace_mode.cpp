#include "trace_mode.h"

#include "bench_args.h"
#include "bench_can_tx.h"
#include "bench_runtime.h"
#include "signal_timeline.h"

#include "can/can_rx_route.h"
#include "can/obd2_poller.h"
#include "runtime/signal_store.h"
#include "bench_clock.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr uint32_t kEpochMs = 1000;
constexpr uint32_t kTickMs = 10;
constexpr uint8_t kMaxFrameBytes = 8;
constexpr size_t kLineBytes = 128;

struct TraceFrame {
    uint32_t atMs;
    uint32_t id;
    uint8_t data[kMaxFrameBytes];
    uint8_t length;
};

FILE *s_out = nullptr;

uint32_t elapsedMs() {
    return BenchClock::nowMs() - kEpochMs;
}

void recordTx(uint32_t id, const uint8_t *data, uint8_t length) {
    SignalTimeline::writeFrame(s_out, elapsedMs(), "tx", id, data, length);
}

void tickFirmware() {
    Obd2Poller::tick(BenchClock::nowMs());
    SignalStore::checkTimeouts();
    SignalTimeline::writeChanges(s_out, elapsedMs());
}

void advanceTo(uint32_t atMs) {
    while (elapsedMs() < atMs) {
        const uint32_t step = atMs - elapsedMs() < kTickMs ? atMs - elapsedMs() : kTickMs;
        BenchClock::advanceMs(step);
        tickFirmware();
    }
}

bool parseHexPayload(const char *hex, TraceFrame &frame) {
    const size_t digits = strlen(hex);
    if (digits % 2 != 0 || digits / 2 > kMaxFrameBytes)
        return false;
    frame.length = static_cast<uint8_t>(digits / 2);
    for (uint8_t i = 0; i < frame.length; ++i) {
        const char pair[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};
        frame.data[i] = static_cast<uint8_t>(strtoul(pair, nullptr, 16));
    }
    return true;
}

bool parseLine(const char *line, TraceFrame &frame, bool &isEnd) {
    char verb[8] = {};
    char hex[2 * kMaxFrameBytes + 2] = {};
    unsigned id = 0;
    const int fields = sscanf(line, "%u %7s %x %17s", &frame.atMs, verb, &id, hex);
    isEnd = fields == 2 && strcmp(verb, "end") == 0;
    if (isEnd)
        return true;
    frame.id = id;
    return fields >= 3 && strcmp(verb, "rx") == 0 && parseHexPayload(fields == 4 ? hex : "", frame);
}

void deliver(const TraceFrame &frame) {
    SignalTimeline::writeFrame(s_out, frame.atMs, "rx", frame.id, frame.data, frame.length);
    CanRxRoute::deliver(frame.id, frame.data, frame.length);
    SignalTimeline::writeChanges(s_out, frame.atMs);
}

bool replayLine(const char *line, unsigned lineNumber) {
    TraceFrame frame = {};
    bool isEnd = false;
    if (!parseLine(line, frame, isEnd)) {
        fprintf(stderr, "bench: trace line %u is not '<ms> rx <id> <hex>' or '<ms> end'\n",
                lineNumber);
        return false;
    }
    advanceTo(frame.atMs);
    if (!isEnd)
        deliver(frame);
    return true;
}

bool replay(FILE *trace) {
    char line[kLineBytes];
    unsigned lineNumber = 0;
    while (fgets(line, sizeof(line), trace) != nullptr) {
        ++lineNumber;
        if (!replayLine(line, lineNumber))
            return false;
    }
    return true;
}

bool bootTimeline(const char *dataRoot) {
    BenchClock::useManual(kEpochMs);
    if (!BenchRuntime::boot(dataRoot))
        return false;
    Obd2Poller::init();
    SignalTimeline::reset();
    BenchCanTx::setSink(&recordTx);
    return true;
}

}

namespace TraceMode {

int run(int argc, char **argv) {
    const char *tracePath = BenchArgs::value(argc, argv, "--trace", nullptr);
    const char *outPath = BenchArgs::value(argc, argv, "--out", nullptr);
    if (tracePath == nullptr || outPath == nullptr)
        return 2;
    FILE *trace = fopen(tracePath, "r");
    s_out = fopen(outPath, "w");
    const bool opened = trace != nullptr && s_out != nullptr;
    const bool ok =
        opened && bootTimeline(BenchArgs::value(argc, argv, "--data", "data")) && replay(trace);
    if (trace != nullptr)
        fclose(trace);
    if (s_out != nullptr)
        fclose(s_out);
    return ok ? 0 : 1;
}

}
