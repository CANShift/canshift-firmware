#pragma once

#include "app_config.h"

namespace WorkshopServer {

#if APP_WORKSHOP_ENABLED

void requestStart();

void tick();

#else

inline void requestStart() {}

inline void tick() {}

#endif

}
