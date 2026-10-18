#include "lvgl_pool_alloc.h"

#include "diag/logger.h"

#include <Arduino.h>
#include <esp_system.h>

#ifdef SIM_BUILD

    #include <stdlib.h>

extern "C" void *canshift_lvgl_pool_alloc(size_t size) {
    void *pool = malloc(size);
    if (pool == nullptr)
        abort();
    return pool;
}

#else

    #include <esp_heap_caps.h>
    #include <multi_heap.h>

    #ifndef LVGL_POOL_FORCE_FAIL
        #define LVGL_POOL_FORCE_FAIL 0
    #endif

namespace {

constexpr size_t kPsramPoolBytes = 512U * 1024U;
constexpr size_t kInternalPoolBytes = 80U * 1024U;
constexpr size_t kInternalHeadroomBytes = 16U * 1024U;
constexpr size_t kMinPoolBytes = 32U * 1024U;
constexpr uint32_t kPsramCaps = MALLOC_CAP_SPIRAM;
constexpr uint32_t kInternalCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
constexpr uint32_t kRestartFlushMs = 50;

enum class Tier : uint8_t { Psram = 0, Internal = 1 };

constexpr const char *kTierNames[] = {"PSRAM", "internal RAM"};

multi_heap_handle_t s_heap = nullptr;
size_t s_poolBytes = 0;
Tier s_tier = Tier::Psram;

bool tierAllowed(Tier tier) {
    return static_cast<uint8_t>(tier) >= LVGL_POOL_FORCE_FAIL;
}

multi_heap_handle_t claim(size_t size, Tier tier, uint32_t caps) {
    void *region = heap_caps_malloc(size, caps);
    if (region == nullptr)
        return nullptr;
    multi_heap_handle_t heap = multi_heap_register(region, size);
    if (heap == nullptr) {
        heap_caps_free(region);
        return nullptr;
    }
    s_poolBytes = size;
    s_tier = tier;
    return heap;
}

multi_heap_handle_t claimPsram() {
    if (!tierAllowed(Tier::Psram) || heap_caps_get_largest_free_block(kPsramCaps) < kPsramPoolBytes)
        return nullptr;
    return claim(kPsramPoolBytes, Tier::Psram, kPsramCaps);
}

size_t internalTarget() {
    const size_t largest = heap_caps_get_largest_free_block(kInternalCaps);
    const size_t room = largest > kInternalHeadroomBytes ? largest - kInternalHeadroomBytes : 0;
    const size_t target = room < kInternalPoolBytes ? room : kInternalPoolBytes;
    return target > kMinPoolBytes ? target : kMinPoolBytes;
}

multi_heap_handle_t claimInternal() {
    if (!tierAllowed(Tier::Internal))
        return nullptr;
    for (size_t size = internalTarget(); size >= kMinPoolBytes; size /= 2) {
        multi_heap_handle_t heap = claim(size, Tier::Internal, kInternalCaps);
        if (heap != nullptr)
            return heap;
    }
    return nullptr;
}

[[noreturn]] void restartUnpooled() {
    LOG_ERROR("LVGL", "no pool: not even %u B of internal RAM is free — restarting",
              static_cast<unsigned>(kMinPoolBytes));
    delay(kRestartFlushMs);
    esp_restart();
    for (;;) {}
}

multi_heap_handle_t pool() {
    if (s_heap != nullptr)
        return s_heap;
    s_heap = claimPsram();
    if (s_heap == nullptr)
        s_heap = claimInternal();
    if (s_heap == nullptr)
        restartUnpooled();
    LOG_INFO("LVGL", "pool: %u B in %s", static_cast<unsigned>(s_poolBytes),
             kTierNames[static_cast<uint8_t>(s_tier)]);
    return s_heap;
}

}

extern "C" void *canshift_lv_malloc(size_t size) {
    return multi_heap_malloc(pool(), size);
}

extern "C" void canshift_lv_free(void *ptr) {
    if (ptr == nullptr)
        return;
    multi_heap_free(pool(), ptr);
}

extern "C" void *canshift_lv_realloc(void *ptr, size_t size) {
    return multi_heap_realloc(pool(), ptr, size);
}

extern "C" bool canshift_lvgl_pool_info(CanshiftLvglPoolInfo *out) {
    if (out == nullptr || s_heap == nullptr)
        return false;
    multi_heap_info_t info = {};
    multi_heap_get_info(s_heap, &info);
    out->total = s_poolBytes;
    out->free = info.total_free_bytes;
    out->largest = info.largest_free_block;
    return true;
}

#endif
