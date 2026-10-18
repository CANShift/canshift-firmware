#pragma once

#include "app_config.h"

namespace WorkshopScreen {

#if APP_WORKSHOP_ENABLED

[[nodiscard]] bool enter();

#else

[[nodiscard]] inline bool enter() {
    return false;
}

#endif

}
