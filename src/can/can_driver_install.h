#pragma once

#include <esp_err.h>

namespace CanDriverInstall {

void reserveTaskStack();

[[nodiscard]] esp_err_t installOnCore0(bool *installed);

}
