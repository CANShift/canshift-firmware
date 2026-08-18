#include "ui/widgets/timer_sources.h"

#include <stdio.h>
#include <string.h>

namespace {

constexpr char kNoValue[] = "--:--";
constexpr char kNoDelta[] = "--";
constexpr uint32_t kMsPerSecond = 1000;
constexpr uint32_t kSecondsPerMinute = 60;
constexpr uint32_t kMsPerHundredth = 10;
constexpr int32_t kDeltaClampMs = 999990;

void writeLapTime(uint32_t ms, char *buf, size_t cap) {
    if (ms == 0) {
        strlcpy(buf, kNoValue, cap);
        return;
    }
    const uint32_t totalSeconds = ms / kMsPerSecond;
    snprintf(buf, cap, "%lu:%02lu.%02lu",
             static_cast<unsigned long>(totalSeconds / kSecondsPerMinute),
             static_cast<unsigned long>(totalSeconds % kSecondsPerMinute),
             static_cast<unsigned long>((ms % kMsPerSecond) / kMsPerHundredth));
}

void renderElapsed(const TimerSources::Inputs &in, char *buf, size_t cap) {
    writeLapTime(in.elapsedMs, buf, cap);
}

void renderLap(const TimerSources::Inputs &in, char *buf, size_t cap) {
    writeLapTime(in.trackActive ? in.currentLapMs : in.elapsedMs, buf, cap);
}

void renderBest(const TimerSources::Inputs &in, char *buf, size_t cap) {
    writeLapTime(in.bestLapMs, buf, cap);
}

void renderLast(const TimerSources::Inputs &in, char *buf, size_t cap) {
    writeLapTime(in.lastLapMs, buf, cap);
}

void renderLapCount(const TimerSources::Inputs &in, char *buf, size_t cap) {
    snprintf(buf, cap, "%u",
             static_cast<unsigned>(in.trackActive ? in.trackLapNumber : in.stopwatchLaps));
}

void renderDelta(const TimerSources::Inputs &in, char *buf, size_t cap) {
    if (!in.trackActive) {
        strlcpy(buf, kNoDelta, cap);
        return;
    }
    const int32_t clamped = in.deltaMs > kDeltaClampMs    ? kDeltaClampMs
                            : in.deltaMs < -kDeltaClampMs ? -kDeltaClampMs
                                                          : in.deltaMs;
    const uint32_t magnitude = static_cast<uint32_t>(clamped < 0 ? -clamped : clamped);
    snprintf(buf, cap, "%c%lu.%02lu", clamped < 0 ? '-' : '+',
             static_cast<unsigned long>(magnitude / kMsPerSecond),
             static_cast<unsigned long>((magnitude % kMsPerSecond) / kMsPerHundredth));
}

struct SourceRenderer {
    CfgTimerSource source;
    void (*render)(const TimerSources::Inputs &, char *, size_t);
    const char *kicker;
};

constexpr SourceRenderer kRenderers[] = {
    {CfgTimerSource::Elapsed, &renderElapsed, "TIMER"},
    {CfgTimerSource::Lap, &renderLap, "LAP"},
    {CfgTimerSource::Best, &renderBest, "BEST"},
    {CfgTimerSource::Last, &renderLast, "LAST"},
    {CfgTimerSource::LapCount, &renderLapCount, "LAPS"},
    {CfgTimerSource::Delta, &renderDelta, "DELTA"},
};

const SourceRenderer *rendererFor(CfgTimerSource source) {
    for (const SourceRenderer &entry : kRenderers) {
        if (entry.source == source)
            return &entry;
    }
    return &kRenderers[0];
}

} // namespace

namespace TimerSources {

void render(CfgTimerSource source, const Inputs &in, char *buf, size_t cap) {
    if (buf == nullptr || cap == 0)
        return;
    rendererFor(source)->render(in, buf, cap);
}

const char *kicker(CfgTimerSource source) {
    return rendererFor(source)->kicker;
}

bool isInteractive(CfgTimerSource source) {
    return source == CfgTimerSource::Elapsed;
}

} // namespace TimerSources
