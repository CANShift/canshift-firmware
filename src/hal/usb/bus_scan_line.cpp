#include "bus_scan_line.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

namespace {

constexpr int kBusScanSteps = kBusScanRateCount + 1;

constexpr const char *kPhaseNames[] = {"idle", "probing", "collecting", "done"};
constexpr const char *kOutcomeNames[] = {"none", "detected", "silent", "unclear", "failed"};

static_assert(sizeof(kPhaseNames) / sizeof(kPhaseNames[0]) == kBusScanPhaseDone + 1,
              "every bus scan phase needs a wire name");
static_assert(sizeof(kOutcomeNames) / sizeof(kOutcomeNames[0]) == kBusScanOutcomeFailed + 1,
              "every bus scan outcome needs a wire name");

class LineWriter {
  public:
    LineWriter(char *out, size_t cap) : out_(out), cap_(cap) {}

    __attribute__((format(printf, 2, 3))) void add(const char *fmt, ...) {
        if (!ok_)
            return;
        va_list args;
        va_start(args, fmt);
        const int n = vsnprintf(out_ + used_, cap_ - used_, fmt, args);
        va_end(args);
        ok_ = n >= 0 && static_cast<size_t>(n) < cap_ - used_;
        used_ += ok_ ? static_cast<size_t>(n) : 0;
    }

    [[nodiscard]] size_t finish() const {
        return ok_ ? used_ : 0;
    }

  private:
    char *out_;
    size_t cap_;
    size_t used_ = 0;
    bool ok_ = true;
};

const char *nameOf(const char *const *names, size_t count, uint8_t index) {
    return index < count ? names[index] : names[0];
}

int stepOf(const BusScanRs &scan) {
    return scan.phase == kBusScanPhaseCollecting ? kBusScanSteps : scan.probe_index + 1;
}

void addProgress(LineWriter &line, const BusScanRs &scan) {
    line.add(",\"kbps\":%u,\"step\":%d,\"steps\":%d", static_cast<unsigned>(scan.install_kbps),
             stepOf(scan), kBusScanSteps);
}

void addProbes(LineWriter &line, const BusScanRs &scan) {
    line.add(",\"probes\":[");
    for (int i = 0; i < kBusScanRateCount; ++i) {
        const BusScanProbeRs &probe = scan.probes[i];
        line.add("%s[%u,%lu,%lu]", i == 0 ? "" : ",", static_cast<unsigned>(probe.kbps),
                 static_cast<unsigned long>(probe.frames),
                 static_cast<unsigned long>(probe.bus_errors));
    }
    line.add("]");
}

void addIds(LineWriter &line, const BusScanRs &scan) {
    const uint16_t count = scan.id_count < kBusScanIdCapacity
                               ? scan.id_count
                               : static_cast<uint16_t>(kBusScanIdCapacity);
    line.add(",\"ids\":[");
    for (uint16_t i = 0; i < count; ++i) {
        line.add("%s[%lu,%lu]", i == 0 ? "" : ",", static_cast<unsigned long>(scan.ids[i].id),
                 static_cast<unsigned long>(scan.ids[i].count));
    }
    line.add("]");
}

void addResult(LineWriter &line, const BusScanRs &scan) {
    const bool detected = scan.outcome == kBusScanOutcomeDetected;
    line.add(",\"outcome\":\"%s\",\"kbps\":%u,\"window_ms\":%lu",
             nameOf(kOutcomeNames, sizeof(kOutcomeNames) / sizeof(kOutcomeNames[0]), scan.outcome),
             detected ? static_cast<unsigned>(scan.install_kbps) : 0U,
             static_cast<unsigned long>(kBusScanCollectMs));
    addProbes(line, scan);
    addIds(line, scan);
    line.add(",\"truncated\":%s", scan.truncated ? "true" : "false");
}

void addBody(LineWriter &line, const BusScanRs &scan) {
    if (scan.phase == kBusScanPhaseDone) {
        addResult(line, scan);
        return;
    }
    if (scan.phase != kBusScanPhaseIdle)
        addProgress(line, scan);
}

}

size_t BusScanLine::format(const BusScanRs &scan, char *out, size_t cap) {
    if (out == nullptr || cap == 0)
        return 0;
    LineWriter line(out, cap);
    line.add("{\"status\":\"ok\",\"state\":\"%s\"",
             nameOf(kPhaseNames, sizeof(kPhaseNames) / sizeof(kPhaseNames[0]), scan.phase));
    addBody(line, scan);
    line.add("}");
    return line.finish();
}
