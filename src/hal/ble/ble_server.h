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

void setPendingEnabled(bool enabled);

int8_t takePendingEnabled();

void tick();

bool isConnected();

void pushStatusNotify();

} // namespace BleServer

#endif
