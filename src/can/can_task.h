#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace CanTask {

[[nodiscard]] bool reserveStack();

[[nodiscard]] bool start();

[[nodiscard]] TaskHandle_t handle();

}
