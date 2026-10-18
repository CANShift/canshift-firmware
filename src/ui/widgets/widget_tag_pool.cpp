#include "ui/widgets/widget_tag_pool.h"

#include "app_config.h"

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <stddef.h>

static_assert(WidgetTagPool::kPoolSlots == kConfigMaxWidgetsPerPage,
              "Pool capacity must mirror CONFIG_MAX_WIDGETS_PER_PAGE — a page is "
              "bounded to that many widgets total");

extern SemaphoreHandle_t g_lvglMutex;

namespace {

using SlotStorage = uint8_t[WidgetTagPool::kSlotBytes];

alignas(WidgetTagPool::kSlotAlign) SlotStorage s_pagePool[WidgetTagPool::kPoolSlots];
bool s_pageBusy[WidgetTagPool::kPoolSlots] = {};

SlotStorage *s_pool = s_pagePool;
bool *s_busy = s_pageBusy;
size_t s_capacity = WidgetTagPool::kPoolSlots;

inline void assertUiThreadHoldsLvglMutex() {
    configASSERT(xPortGetCoreID() == kTaskCoreUi);
    configASSERT(g_lvglMutex != nullptr);
    const TaskHandle_t holder = xSemaphoreGetMutexHolder(g_lvglMutex);
    configASSERT(holder == nullptr || holder == xTaskGetCurrentTaskHandle());
}

bool anySlotBusy() {
    for (size_t i = 0; i < s_capacity; ++i) {
        if (s_busy[i])
            return true;
    }
    return false;
}

}

namespace WidgetTagPool {

bool reserveResidentSlots(size_t slots) {
    if (slots <= s_capacity)
        return true;
    if (anySlotBusy())
        return false;
    auto *pool = static_cast<SlotStorage *>(
        heap_caps_aligned_alloc(kSlotAlign, slots * kSlotBytes, MALLOC_CAP_SPIRAM));
    auto *busy = static_cast<bool *>(heap_caps_calloc(slots, sizeof(bool), MALLOC_CAP_SPIRAM));
    if (!pool || !busy) {
        heap_caps_free(pool);
        heap_caps_free(busy);
        return false;
    }
    s_pool = pool;
    s_busy = busy;
    s_capacity = slots;
    return true;
}

size_t capacity() {
    return s_capacity;
}

void *allocRaw() {
    assertUiThreadHoldsLvglMutex();
    for (size_t i = 0; i < s_capacity; ++i) {
        if (!s_busy[i]) {
            s_busy[i] = true;
            return s_pool[i];
        }
    }
    return nullptr;
}

void releaseRaw(void *p) {
    assertUiThreadHoldsLvglMutex();
    for (size_t i = 0; i < s_capacity; ++i) {
        if (static_cast<void *>(s_pool[i]) == p) {
            s_busy[i] = false;
            return;
        }
    }
}

}
