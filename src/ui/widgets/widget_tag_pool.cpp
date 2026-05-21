// widget_tag_pool.cpp — Single shared slab of widget Tag storage.
//
// See widget_tag_pool.h for the design rationale.

#include "ui/widgets/widget_tag_pool.h"

#include "app_config.h"

#include <stddef.h>

static_assert(WidgetTagPool::kPoolSlots == CONFIG_MAX_WIDGETS_PER_PAGE,
              "Pool capacity must mirror CONFIG_MAX_WIDGETS_PER_PAGE — a page is "
              "bounded to that many widgets total");

namespace {

alignas(
    WidgetTagPool::kSlotAlign) uint8_t s_pool[WidgetTagPool::kPoolSlots][WidgetTagPool::kSlotBytes];

bool s_busy[WidgetTagPool::kPoolSlots] = {};

} // namespace

namespace WidgetTagPool {

void *allocRaw() {
    for (size_t i = 0; i < kPoolSlots; ++i) {
        if (!s_busy[i]) {
            s_busy[i] = true;
            return s_pool[i];
        }
    }
    return nullptr;
}

void releaseRaw(void *p) {
    for (size_t i = 0; i < kPoolSlots; ++i) {
        if (static_cast<void *>(s_pool[i]) == p) {
            s_busy[i] = false;
            return;
        }
    }
}

} // namespace WidgetTagPool
