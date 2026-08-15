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
LV_FONT_DECLARE(lv_font_jbmono_extrabold_84_nk);
LV_FONT_DECLARE(lv_font_jbmono_medium_10_nk);
LV_FONT_DECLARE(lv_font_archivo_extrabold_13_nk);
LV_FONT_DECLARE(lv_font_archivo_extrabold_14_nk);
LV_FONT_DECLARE(lv_font_archivo_extrabold_15_nk);

namespace {

struct Face {
    uint8_t size;
    const lv_font_t *inFlash;
};

constexpr Face kValueFaces[] = {{17, nullptr},
                                {22, nullptr},
                                {32, &lv_font_jbmono_extrabold_32_nk},
                                {44, &lv_font_jbmono_extrabold_44_nk},
                                {48, &lv_font_jbmono_extrabold_48_nk},
                                {84, &lv_font_jbmono_extrabold_84_nk}};

constexpr Face kLabelFaces[] = {{10, nullptr},
                                {12, nullptr},
                                {13, &lv_font_archivo_extrabold_13_nk},
                                {14, &lv_font_archivo_extrabold_14_nk},
                                {15, &lv_font_archivo_extrabold_15_nk},
                                {16, nullptr}};

constexpr size_t kValueCount = sizeof(kValueFaces) / sizeof(kValueFaces[0]);
constexpr size_t kLabelCount = sizeof(kLabelFaces) / sizeof(kLabelFaces[0]);

const lv_font_t *s_value[kValueCount] = {nullptr};
const lv_font_t *s_label[kLabelCount] = {nullptr};
bool s_initialized = false;

size_t snapIndex(const Face *faces, size_t count, uint8_t size) {
    size_t idx = 0;
    for (size_t i = 0; i < count; ++i) {
        if (faces[i].size <= size) {
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

const lv_font_t *resolve(const Face *faces, size_t count, const lv_font_t *const *cache,
                         uint8_t size, const lv_font_t *fallback) {
    const size_t idx = snapIndex(faces, count, size);
    const lv_font_t *const cached = cache[idx];
    return (cached != nullptr) ? cached : fallback;
}

void loadFamily(const char *family, const char *weight, const char *intent, const Face *faces,
                size_t count, const lv_font_t **cache) {
    for (size_t i = 0; i < count; ++i) {
        if (faces[i].inFlash != nullptr) {
            cache[i] = faces[i].inFlash;
            continue;
        }
        loadOne(family, weight, intent, faces[i].size, cache[i]);
    }
}

void freeLoaded(const Face *faces, size_t count, const lv_font_t **cache) {
    for (size_t i = 0; i < count; ++i) {
        if (cache[i] != nullptr && faces[i].inFlash == nullptr) {
            lv_font_free(const_cast<lv_font_t *>(cache[i]));
        }
        cache[i] = nullptr;
    }
}

} // namespace

void FontManager::init() {
    if (s_initialized) {
        return;
    }
    LOG_INFO("FONT", "Loading font family 'jbmono' (device scale)");

    loadFamily("jbmono", "extrabold", "value", kValueFaces, kValueCount, s_value);
    loadFamily("archivo", "extrabold", "label", kLabelFaces, kLabelCount, s_label);

    s_initialized = true;
}

void FontManager::shutdown() {
    freeLoaded(kValueFaces, kValueCount, s_value);
    freeLoaded(kLabelFaces, kLabelCount, s_label);
    s_initialized = false;
}

const lv_font_t *FontManager::value(uint8_t devicePx) {
    return resolve(kValueFaces, kValueCount, s_value, devicePx, &lv_font_jbmono_medium_10_nk);
}

const lv_font_t *FontManager::units() {
    return &lv_font_jbmono_medium_10_nk;
}

const lv_font_t *FontManager::label(uint8_t size) {
    return resolve(kLabelFaces, kLabelCount, s_label, size, &lv_font_archivo_extrabold_14_nk);
}
