#pragma once

#include <lvgl.h>
#include "config/config_types.h"
#include "runtime/signal_store.h"

namespace WidgetFactory {

lv_obj_t *create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset = 0);

void updateAll(lv_obj_t *parent, const SignalStore::SignalValue *snap);

void clearAll(lv_obj_t *parent);

} // namespace WidgetFactory
