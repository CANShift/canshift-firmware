#include "bench_runtime.h"

#include "reply_capture.h"

#include "can/can_parser.h"
#include "config/config_loader.h"
#include "diag/error_store.h"
#include "diag/logger.h"
#include "runtime/signal_store.h"

#include <cstdio>

extern void benchStorageSetRoot(const char *rootDir);

namespace BenchRuntime {

bool boot(const char *dataRoot) {
    Logger::init();
    benchStorageSetRoot(dataRoot);
    const ConfigLoader::LoadResult cfg = ConfigLoader::loadAll();
    if (!cfg.signalsOk) {
        fprintf(stderr, "bench: signals.json under '%s' did not load\n", dataRoot);
        return false;
    }
    SignalStore::init();
    ErrorStore::clear();
    CanParser::loadSignalDefinitions();
    ReplyCapture::install();
    return true;
}

}
