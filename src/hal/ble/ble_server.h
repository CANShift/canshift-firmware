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

void requestStop();

bool takeStopRequest();

    #if APP_WORKSHOP_ENABLED

void requestRelease();

bool takeReleaseRequest();

void release();

    #else

inline void requestRelease() {}

inline bool takeReleaseRequest() {
    return false;
}

inline void release() {}

    #endif

void tick();

bool isConnected();

void pushStatusNotify();

}

#endif
