#pragma once

#include "app_config.h"

#if APP_SD_DATALOG_ENABLED

namespace SdCard {

inline constexpr char kMountPoint[] = "/sdcard";
inline constexpr char kLogDir[] = "/sdcard/logs";

[[nodiscard]] bool slotPresent();

[[nodiscard]] bool mount();

void unmount();

[[nodiscard]] bool mounted();

}

#endif
