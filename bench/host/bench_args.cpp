#include "bench_args.h"

#include <cstring>

namespace BenchArgs {

const char *value(int argc, char **argv, const char *flag, const char *fallback) {
    for (int i = 0; i + 1 < argc; ++i) {
        if (strcmp(argv[i], flag) == 0)
            return argv[i + 1];
    }
    return fallback;
}

bool present(int argc, char **argv, const char *flag) {
    for (int i = 0; i < argc; ++i) {
        if (strcmp(argv[i], flag) == 0)
            return true;
    }
    return false;
}

} // namespace BenchArgs
