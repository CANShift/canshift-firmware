#include "perf_mode.h"
#include "trace_mode.h"
#include "transcript_mode.h"

#include <cstdio>
#include <cstring>

namespace {

struct Mode {
    const char *name;
    int (*run)(int argc, char **argv);
};

constexpr Mode kModes[] = {
    {"perf", &PerfMode::run},
    {"trace", &TraceMode::run},
    {"transcript", &TranscriptMode::run},
};

int usage() {
    fprintf(stderr, "usage: program <mode> [options]\nmodes:");
    for (const Mode &mode : kModes)
        fprintf(stderr, " %s", mode.name);
    fprintf(stderr, "\n");
    return 2;
}

}

int main(int argc, char **argv) {
    if (argc < 2)
        return usage();
    for (const Mode &mode : kModes) {
        if (strcmp(mode.name, argv[1]) == 0)
            return mode.run(argc - 2, argv + 2);
    }
    return usage();
}
