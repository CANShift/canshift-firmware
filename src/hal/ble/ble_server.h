#pragma once

#include "app_config.h"
#include <stdbool.h>
#include <stdint.h>

#if APP_BLE_ENABLED

namespace BleServer {

void earlyInit();

void init();

void start();

void stop();

bool isEnabled();

// Set when the stack fails to come up, so the BLE task tears it down and
// gives the heap back. Nothing asks for a restart — BLE is on whenever it
// is compiled in and the DRAM is there.
void requestStop();

bool takeStopRequest();

void tick();

bool isConnected();

void pushStatusNotify();

} // namespace BleServer

#endif
