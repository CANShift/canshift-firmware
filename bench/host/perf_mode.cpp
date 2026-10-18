#include "perf_mode.h"

#include "bench_args.h"
#include "bench_runtime.h"
#include "perf_cases.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr uint32_t kDefaultSamples = 25;

bool selected(const PerfCases::Case &c, const char *filter) {
    return filter == nullptr || strstr(c.name, filter) != nullptr;
}

void writeCase(FILE *out, const PerfCases::Case &c, const PerfTimer::Stats &s, bool first) {
    fprintf(out,
            "%s\n    {\"name\": \"%s\", \"unit\": \"%s\", \"median_ns\": %.3f, \"p25_ns\": %.3f, "
            "\"p75_ns\": %.3f, \"iterations\": %u, \"samples\": %u}",
            first ? "" : ",", c.name, c.unit, s.medianNs, s.p25Ns, s.p75Ns, s.iterations,
            s.samples);
}

bool measureCase(FILE *out, const PerfCases::Case &c, uint32_t samples, bool first) {
    if (!c.setup(c.arg)) {
        fprintf(stderr, "bench: setup failed for '%s'\n", c.name);
        return false;
    }
    const PerfTimer::Stats stats = PerfTimer::measure(c.run, samples);
    writeCase(out, c, stats, first);
    fprintf(stderr, "%-22s %12.1f %s\n", c.name, stats.medianNs, c.unit);
    return true;
}

bool measureAll(FILE *out, uint32_t samples, const char *filter) {
    const PerfCases::CaseList cases = PerfCases::all();
    bool first = true;
    for (size_t i = 0; i < cases.count; ++i) {
        if (!selected(cases.items[i], filter))
            continue;
        if (!measureCase(out, cases.items[i], samples, first))
            return false;
        first = false;
    }
    return true;
}

}

namespace PerfMode {

int run(int argc, char **argv) {
    const char *outPath = BenchArgs::value(argc, argv, "--out", nullptr);
    const uint32_t samples =
        static_cast<uint32_t>(strtoul(BenchArgs::value(argc, argv, "--samples", "0"), nullptr, 10));
    if (outPath == nullptr || !BenchRuntime::boot(BenchArgs::value(argc, argv, "--data", "data")))
        return 2;
    FILE *out = fopen(outPath, "w");
    if (out == nullptr) {
        fprintf(stderr, "bench: cannot write '%s'\n", outPath);
        return 1;
    }
    fprintf(out, "{\n  \"schema\": 1,\n  \"cases\": [");
    const bool ok = measureAll(out, samples > 0 ? samples : kDefaultSamples,
                               BenchArgs::value(argc, argv, "--filter", nullptr));
    fprintf(out, "\n  ]\n}\n");
    fclose(out);
    return ok ? 0 : 1;
}

}
