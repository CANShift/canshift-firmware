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

bool writeLapTime(uint32_t ms, char *buf, size_t cap) {
    if (ms == 0) {
        strlcpy(buf, kNoValue, cap);
        return false;
    }
    const uint32_t totalSeconds = ms / kMsPerSecond;
    snprintf(buf, cap, "%lu:%02lu.%02lu",
             static_cast<unsigned long>(totalSeconds / kSecondsPerMinute),
             static_cast<unsigned long>(totalSeconds % kSecondsPerMinute),
             static_cast<unsigned long>((ms % kMsPerSecond) / kMsPerHundredth));
    return true;
}

bool renderElapsed(const TimerSources::Inputs &in, char *buf, size_t cap) {
    return writeLapTime(in.elapsedMs, buf, cap);
}

bool renderLap(const TimerSources::Inputs &in, char *buf, size_t cap) {
    return writeLapTime(in.currentLapMs, buf, cap);
}

bool renderBest(const TimerSources::Inputs &in, char *buf, size_t cap) {
    return writeLapTime(in.bestLapMs, buf, cap);
}

bool renderLast(const TimerSources::Inputs &in, char *buf, size_t cap) {
    return writeLapTime(in.lastLapMs, buf, cap);
}

bool renderLapCount(const TimerSources::Inputs &in, char *buf, size_t cap) {
    snprintf(buf, cap, "%u", static_cast<unsigned>(in.lapCount));
    return true;
}

bool renderDelta(const TimerSources::Inputs &in, char *buf, size_t cap) {
    if (!in.trackActive) {
        strlcpy(buf, kNoDelta, cap);
        return false;
    }
    const int32_t clamped = in.deltaMs > kDeltaClampMs    ? kDeltaClampMs
                            : in.deltaMs < -kDeltaClampMs ? -kDeltaClampMs
                                                          : in.deltaMs;
    const uint32_t magnitude = static_cast<uint32_t>(clamped < 0 ? -clamped : clamped);
    snprintf(buf, cap, "%c%lu.%02lu", clamped < 0 ? '-' : '+',
             static_cast<unsigned long>(magnitude / kMsPerSecond),
             static_cast<unsigned long>((magnitude % kMsPerSecond) / kMsPerHundredth));
    return true;
}

bool renderLaunchSplit(const TimerSources::LaunchReading &launch, uint32_t splitMs, char *buf,
                       size_t cap) {
    if (splitMs != 0)
        return writeLapTime(splitMs, buf, cap);
    return writeLapTime(launch.running ? launch.liveMs : 0, buf, cap);
}

bool renderLaunchSprint(const TimerSources::Inputs &in, char *buf, size_t cap) {
    return renderLaunchSplit(in.launch, in.launch.sprintMs, buf, cap);
}

bool renderLaunchStrip(const TimerSources::Inputs &in, char *buf, size_t cap) {
    return renderLaunchSplit(in.launch, in.launch.stripMs, buf, cap);
}

struct SourceRenderer {
    CfgTimerSource source;
    bool (*render)(const TimerSources::Inputs &, char *, size_t);
    const char *kicker;
    const char *imperialKicker;
    const char *unit;
};

constexpr SourceRenderer kRenderers[] = {
    {CfgTimerSource::Elapsed, &renderElapsed, "TIMER", "TIMER", ""},
    {CfgTimerSource::Lap, &renderLap, "LAP", "LAP", ""},
    {CfgTimerSource::Best, &renderBest, "BEST", "BEST", ""},
    {CfgTimerSource::Last, &renderLast, "LAST", "LAST", ""},
    {CfgTimerSource::LapCount, &renderLapCount, "LAPS", "LAPS", ""},
    {CfgTimerSource::Delta, &renderDelta, "DELTA", "DELTA", "s"},
    {CfgTimerSource::LaunchSprint, &renderLaunchSprint, "0-100", "0-60", ""},
    {CfgTimerSource::LaunchStrip, &renderLaunchStrip, "400 M", "1/4 MI", ""},
};

const SourceRenderer *rendererFor(CfgTimerSource source) {
    for (const SourceRenderer &entry : kRenderers) {
        if (entry.source == source)
            return &entry;
    }
    return &kRenderers[0];
}

}

namespace TimerSources {

Inputs select(const TimerService::Snapshot &stopwatch, const TrackStore::State &track,
              bool trackActive) {
    if (trackActive) {
        return {stopwatch.elapsedMs,
                track.currentLapMs,
                track.lastLapMs,
                track.bestLapMs,
                track.lapNumber,
                track.deltaMs,
                true,
                {}};
    }
    return {stopwatch.elapsedMs,
            stopwatch.currentLapMs,
            stopwatch.lastLapMs,
            stopwatch.bestLapMs,
            stopwatch.lapCount,
            0,
            false,
            {}};
}

bool render(CfgTimerSource source, const Inputs &in, char *buf, size_t cap) {
    if (buf == nullptr || cap == 0)
        return false;
    return rendererFor(source)->render(in, buf, cap);
}

const char *kicker(CfgTimerSource source, bool imperial) {
    const SourceRenderer *entry = rendererFor(source);
    return imperial ? entry->imperialKicker : entry->kicker;
}

LaunchReading launchReading(const LaunchTimer &timer, uint32_t nowMs) {
    return {timer.state == kLaunchRunning, imu_launch_elapsed_ms_rs(&timer, nowMs), timer.sprint_ms,
            timer.strip_ms};
}

const char *unit(CfgTimerSource source) {
    return rendererFor(source)->unit;
}

void formatStopwatch(char *buf, size_t len, uint32_t elapsedMs, bool msec, bool blinkOn) {
    if (msec) {
        const uint32_t totalS = elapsedMs / kMsPerSecond;
        const uint32_t ms = elapsedMs % kMsPerSecond;
        snprintf(buf, len, "%02lu.%03lu", static_cast<unsigned long>(totalS),
                 static_cast<unsigned long>(ms));
        return;
    }
    const uint32_t totalS = elapsedMs / kMsPerSecond;
    const uint32_t m = totalS / kSecondsPerMinute;
    const uint32_t s = totalS % kSecondsPerMinute;
    const char sep = blinkOn ? ':' : ' ';
    snprintf(buf, len, "%02lu%c%02lu", static_cast<unsigned long>(m), sep,
             static_cast<unsigned long>(s));
}

bool isInteractive(CfgTimerSource source) {
    return source == CfgTimerSource::Elapsed;
}

}
