#pragma once

#include "app_config.h"

#if APP_SD_DATALOG_ENABLED

namespace DatalogTasks {

void start();

void writerTask(void *);

void samplerTask(void *);

[[nodiscard]] bool reserveStaging();

}

#endif
