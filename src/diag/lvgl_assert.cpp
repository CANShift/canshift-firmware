#include "diag/lvgl_assert.h"
#include "diag/logger.h"
#include <esp_system.h>
#include <Arduino.h>

extern "C" void canshift_lvgl_assert_handler(void) {
    LOG_ERROR("LVGL", "assert tripped (likely OOM in lv_mem_alloc) — restarting");
    delay(50);
    esp_restart();
}
