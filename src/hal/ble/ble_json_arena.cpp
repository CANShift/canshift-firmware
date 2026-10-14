#include "app_config.h"
#if APP_BLE_ENABLED

    #include "ble_json_arena.h"

    #include "ble_json_budget.h"
    #include "util/json_arena.h"

    #include <freertos/FreeRTOS.h>
    #include <freertos/semphr.h>

namespace {

constexpr TickType_t kLeaseTimeout = pdMS_TO_TICKS(50);

struct RefusingAllocator : ArduinoJson::Allocator {
    void *allocate(size_t) override {
        return nullptr;
    }
    void deallocate(void *) override {}
    void *reallocate(void *, size_t) override {
        return nullptr;
    }
};

struct Arena {
    JsonArena &allocator;
    StaticSemaphore_t lockBuffer;
    SemaphoreHandle_t lock;
};

StaticJsonArena<BleJsonBudget::kStatusArenaBytes> s_statusBytes;
StaticJsonArena<BleJsonBudget::kCallbackArenaBytes> s_callbackBytes;
RefusingAllocator s_refusing;

Arena *createArenas() {
    static Arena arenas[] = {{s_statusBytes, {}, nullptr}, {s_callbackBytes, {}, nullptr}};
    for (Arena &arena : arenas) {
        arena.lock = xSemaphoreCreateMutexStatic(&arena.lockBuffer);
    }
    return arenas;
}

Arena &arenaFor(BleJsonArena::Slot slot) {
    static Arena *const arenas = createArenas();
    return arenas[static_cast<size_t>(slot)];
}

bool take(BleJsonArena::Slot slot) {
    return xSemaphoreTake(arenaFor(slot).lock, kLeaseTimeout) == pdTRUE;
}

ArduinoJson::Allocator *allocatorFor(BleJsonArena::Slot slot, bool held) {
    if (!held) {
        return &s_refusing;
    }
    return &arenaFor(slot).allocator;
}

} // namespace

BleJsonArena::LeasedDocument::LeasedDocument(Slot slot)
    : slot_(slot), held_(take(slot)), doc_(allocatorFor(slot, held_)) {}

BleJsonArena::LeasedDocument::~LeasedDocument() {
    doc_.clear();
    if (held_) {
        xSemaphoreGive(arenaFor(slot_).lock);
    }
}

#endif
