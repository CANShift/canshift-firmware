#include "font_manager.h"
#include "diag/error_store.h"
#include "diag/logger.h"

#include <Arduino.h>
#include <SPIFFS.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(lv_font_jbmono_extrabold_32_nk);
LV_FONT_DECLARE(lv_font_jbmono_extrabold_44_nk);
LV_FONT_DECLARE(lv_font_jbmono_extrabold_48_nk);
LV_FONT_DECLARE(lv_font_jbmono_medium_10_nk);
LV_FONT_DECLARE(lv_font_archivo_extrabold_14_nk);

namespace {

constexpr uint8_t kValueSizes[] = {17, 22, 32, 44, 48};
constexpr uint8_t kSpiffsValueCount = 2;
constexpr uint8_t kLabelSizes[] = {10, 12, 14, 16};

constexpr size_t kValueCount = sizeof(kValueSizes) / sizeof(kValueSizes[0]);
constexpr size_t kLabelCount = sizeof(kLabelSizes) / sizeof(kLabelSizes[0]);

const lv_font_t *s_value[kValueCount] = {nullptr};
const lv_font_t *s_label[kLabelCount] = {nullptr};
bool s_initialized = false;

size_t snapIndex(const uint8_t *sizes, size_t count, uint8_t size) {
    size_t idx = 0;
    for (size_t i = 0; i < count; ++i) {
        if (sizes[i] <= size) {
            idx = i;
        } else {
            break;
        }
    }
    return idx;
}

void logFontHeap(const char *stage, const char *family, const char *weight, uint8_t size) {
    const uint32_t free = ESP.getFreeHeap();
    const uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    LOG_INFO("FONT", "%s %s_%s_%u: free=%u largest=%u", stage, family, weight,
             static_cast<unsigned>(size), static_cast<unsigned>(free),
             static_cast<unsigned>(largest));
}

bool poolHasRoomFor(const char *spiffsPath, const char *family, const char *weight, uint8_t size) {
    File f = SPIFFS.open(spiffsPath, "r");
    if (!f) {

        return true;
    }
    const size_t fileSize = static_cast<size_t>(f.size());
    f.close();

    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);

    constexpr uint32_t kOverheadBytes = 1024;
    const uint32_t needed = static_cast<uint32_t>(fileSize) + kOverheadBytes;
    if (mon.free_size < needed) {
        LOG_ERROR("FONT",
                  "LVGL pool too small for %s_%s_%u.bin: need ~%u B, "
                  "have %u B free (pool=%u B). Skipping to avoid NULL-deref "
                  "inside lv_font_load.",
                  family, weight, static_cast<unsigned>(size), static_cast<unsigned>(needed),
                  static_cast<unsigned>(mon.free_size), static_cast<unsigned>(mon.total_size));
        char detail[60];
        snprintf(detail, sizeof(detail), "pool too small for %s_%u", weight, size);
        ErrorStore::push(ERROR_SRC_SYSTEM, "FONT_POOL_OOM", detail);
        return false;
    }
    return true;
}

void loadOne(const char *family, const char *weight, const char *intent, uint8_t size,
             const lv_font_t *&slot) {
    char path[64];
    snprintf(path, sizeof(path), "S:/fonts/%s_%s_%u.bin", family, weight, size);

    char spiffsPath[64];
    snprintf(spiffsPath, sizeof(spiffsPath), "/fonts/%s_%s_%u.bin", family, weight, size);

    logFontHeap("before", family, weight, size);

    const lv_font_t *font = nullptr;
    if (poolHasRoomFor(spiffsPath, family, weight, size)) {
        font = lv_font_load(path);
    }

    logFontHeap("after ", family, weight, size);

    if (font == nullptr) {
        LOG_ERROR("FONT", "Failed to load %s_%s_%u.bin from SPIFFS — falling back to built-in",
                  family, weight, size);

        char detail[60];
        snprintf(detail, sizeof(detail), "%s_%s_%u.bin missing", family, weight, size);
        ErrorStore::push(ERROR_SRC_SYSTEM, "FONT_LOAD", detail);
    } else {
        LOG_INFO("FONT", "Loaded %s_%s_%u.bin from SPIFFS (%s)", family, weight, size, intent);
    }
    slot = font;
}

const lv_font_t *resolve(const uint8_t *sizes, size_t count, const lv_font_t *const *cache,
                         uint8_t size, const lv_font_t *fallback) {
    const size_t idx = snapIndex(sizes, count, size);
    const lv_font_t *const cached = cache[idx];
    return (cached != nullptr) ? cached : fallback;
}

} // namespace

void FontManager::init() {
    if (s_initialized) {
        return;
    }
    LOG_INFO("FONT", "Loading font family 'jbmono' (device scale)");

    for (uint8_t i = 0; i < kSpiffsValueCount; ++i) {
        loadOne("jbmono", "extrabold", "value", kValueSizes[i], s_value[i]);
    }
    s_value[2] = &lv_font_jbmono_extrabold_32_nk;
    s_value[3] = &lv_font_jbmono_extrabold_44_nk;
    s_value[4] = &lv_font_jbmono_extrabold_48_nk;
    LOG_INFO("FONT", "jbmono_extrabold_{32,44,48}: using in-flash copies");

    loadOne("archivo", "extrabold", "label", kLabelSizes[0], s_label[0]);
    loadOne("archivo", "extrabold", "label", kLabelSizes[1], s_label[1]);
    s_label[2] = &lv_font_archivo_extrabold_14_nk;
    LOG_INFO("FONT", "archivo_extrabold_14: using in-flash copy");
    loadOne("archivo", "extrabold", "label", kLabelSizes[3], s_label[3]);

    s_initialized = true;
}

void FontManager::shutdown() {
    auto freeAll = [](const lv_font_t **slots, size_t count) {
        for (size_t i = 0; i < count; ++i) {

            const bool isInFlash = slots[i] == &lv_font_jbmono_medium_10_nk ||
                                   slots[i] == &lv_font_archivo_extrabold_14_nk ||
                                   slots[i] == &lv_font_jbmono_extrabold_32_nk ||
                                   slots[i] == &lv_font_jbmono_extrabold_44_nk ||
                                   slots[i] == &lv_font_jbmono_extrabold_48_nk;
            if (slots[i] != nullptr && !isInFlash) {
                lv_font_free(const_cast<lv_font_t *>(slots[i]));
            }
            slots[i] = nullptr;
        }
    };
    freeAll(s_value, kValueCount);
    freeAll(s_label, kLabelCount);
    s_initialized = false;
}

const lv_font_t *FontManager::value(uint8_t devicePx) {
    return resolve(kValueSizes, kValueCount, s_value, devicePx, &lv_font_jbmono_medium_10_nk);
}

const lv_font_t *FontManager::units() {
    return &lv_font_jbmono_medium_10_nk;
}

const lv_font_t *FontManager::label(uint8_t size) {
    return resolve(kLabelSizes, kLabelCount, s_label, size, &lv_font_archivo_extrabold_14_nk);
}
