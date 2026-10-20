#include "app_config.h"
#if APP_SD_DATALOG_ENABLED

    #include "datalog_tasks.h"

    #include "datalog_shared.h"

    #include "diag/error_store.h"
    #include "diag/logger.h"
    #include "hal/storage/sd_card.h"
    #include "runtime/task_spawn.h"

namespace DatalogTasks {

namespace {

bool reserveBuffers() {
    if (DatalogShared::reserve() && reserveStaging())
        return true;
    ErrorStore::push(ERROR_SRC_SYSTEM, "datalog", "no memory for the datalog, logging off");
    return false;
}

}

void start() {
    if (!SdCard::slotPresent()) {
        LOG_INFO("DLOG", "board has no SD slot, datalog off");
        return;
    }
    if (!reserveBuffers())
        return;
    if (TaskSpawn::spawnDynamic("dlog_w", writerTask, TASK_STACK_DATALOG, kTaskPrioDatalogWriter,
                                kTaskCoreDatalogWriter) == nullptr)
        return;
    if (TaskSpawn::spawnDynamic("dlog_s", samplerTask, TASK_STACK_DATALOG, kTaskPrioDatalogSampler,
                                kTaskCoreDatalogSampler) == nullptr)
        return;
    LOG_INFO("DLOG", "datalog armed: %u ms samples, %u KB ring",
             static_cast<unsigned>(kDatalogSamplePeriodMs),
             static_cast<unsigned>(kDatalogRingBytes / 1024U));
}

}

#endif
