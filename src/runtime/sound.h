#pragma once

#include "app_config.h"

namespace Sound {

#if APP_AUDIO_ENABLED

void start();

#else

inline void start() {}

#endif

}
