#pragma once

#include "perf_timer.h"

#include <cstddef>

namespace PerfCases {

struct Case {
    const char *name;
    const char *unit;
    const char *arg;
    bool (*setup)(const char *arg);
    PerfTimer::Op run;
};

struct CaseList {
    const Case *items;
    size_t count;
};

[[nodiscard]] CaseList all();

bool setupNothing(const char *arg);
void runCalibration(uint32_t index);

bool setupCanDecode(const char *arg);
void runCanDecode(uint32_t index);

bool setupExprEval(const char *arg);
void runExprEval(uint32_t index);

void runTelemetryEncode(uint32_t index);
void runStatusWriter(uint32_t index);
void runFormatFloat(uint32_t index);

bool setupCommand(const char *line);
void runCommand(uint32_t index);

} // namespace PerfCases
