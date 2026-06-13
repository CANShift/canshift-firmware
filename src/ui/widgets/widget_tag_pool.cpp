#include "ui/widgets/widget_tag_pool.h"

#include "app_config.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <stddef.h>

static_assert(WidgetTagPool::kPoolSlots == CONFIG_MAX_WIDGETS_PER_PAGE,
              "Pool capacity must mirror CONFIG_MAX_WIDGETS_PER_PAGE — a page is "
              "bounded to that many widgets total");

extern SemaphoreHandle_t g_lvglMutex;

namespace {

alignas(
    WidgetTagPool::kSlotAlign) uint8_t s_pool[WidgetTagPool::kPoolSlots][WidgetTagPool::kSlotBytes];

bool s_busy[WidgetTagPool::kPoolSlots] = {};

inline void assertUiThreadHoldsLvglMutex() {
    configASSERT(xPortGetCoreID() == TASK_CORE_UI);
    configASSERT(g_lvglMutex != nullptr);
    const TaskHandle_t holder = xSemaphoreGetMutexHolder(g_lvglMutex);
    configASSERT(holder == nullptr || holder == xTaskGetCurrentTaskHandle());
}

} // namespace

namespace WidgetTagPool {

void *allocRaw() {
    assertUiThreadHoldsLvglMutex();
    for (size_t i = 0; i < kPoolSlots; ++i) {
        if (!s_busy[i]) {
            s_busy[i] = true;
            return s_pool[i];
        }
    }
    return nullptr;
}

void releaseRaw(void *p) {
    assertUiThreadHoldsLvglMutex();
    for (size_t i = 0; i < kPoolSlots; ++i) {
        if (static_cast<void *>(s_pool[i]) == p) {
            s_busy[i] = false;
            return;
        }
    }
}

} // namespace WidgetTagPool
