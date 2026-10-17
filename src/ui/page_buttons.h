#pragma once

#include "runtime/signal_store.h"

#include <lvgl.h>
#include <stdint.h>

namespace PageButtons {

void init();

[[nodiscard]] lv_dir_t tick(const SignalStore::SignalValue *snap, uint32_t nowMs);

} // namespace PageButtons
