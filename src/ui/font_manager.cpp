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
LV_FONT_DECLARE(lv_font_jbmono_extrabold_64_nk);
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

constexpr Face kValueFaces[] = {{13, nullptr},
                                {15, nullptr},
                                {17, nullptr},
                                {22, nullptr},
                                {32, &lv_font_jbmono_extrabold_32_nk},
                                {44, &lv_font_jbmono_extrabold_44_nk},
                                {48, &lv_font_jbmono_extrabold_48_nk},
                                {64, &lv_font_jbmono_extrabold_64_nk},
                                {84, &lv_font_jbmono_extrabold_84_nk}};

constexpr Face kLabelFaces[] = {{10, nullptr},
                                {12, nullptr},
                                {13, &lv_font_archivo_extrabold_13_nk},
                                {14, &lv_font_archivo_extrabold_14_nk},
                                {15, &lv_font_archivo_extrabold_15_nk},
                                {16, nullptr}};

constexpr size_t kValueCount = sizeof(kValueFaces) / sizeof(kValueFaces[0]);
constexpr size_t kLabelCount = sizeof(kLabelFaces) / sizeof(kLabelFaces[0]);
constexpr uint8_t kSmallestValuePx = kValueFaces[0].size;

struct FaceSlot {
    const lv_font_t *font;
    bool attempted;
};

struct Family {
    const char *name;
    const char *weight;
    const char *intent;
    const Face *faces;
    size_t count;
    FaceSlot *slots;
    const lv_font_t *fallback;
};

FaceSlot s_valueSlots[kValueCount] = {};
FaceSlot s_labelSlots[kLabelCount] = {};

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

void loadOne(const Family &fam, uint8_t size, FaceSlot &slot) {
    const char *family = fam.name;
    const char *weight = fam.weight;
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
        LOG_INFO("FONT", "Loaded %s_%s_%u.bin from SPIFFS (%s)", family, weight, size, fam.intent);
    }
    slot.font = font;
}

const lv_font_t *faceAt(const Family &fam, size_t idx) {
    FaceSlot &slot = fam.slots[idx];
    if (slot.attempted)
        return slot.font != nullptr ? slot.font : fam.fallback;

    slot.attempted = true;
    if (fam.faces[idx].inFlash != nullptr) {
        slot.font = fam.faces[idx].inFlash;
        return slot.font;
    }

    loadOne(fam, fam.faces[idx].size, slot);
    return slot.font != nullptr ? slot.font : fam.fallback;
}

const lv_font_t *resolve(const Family &fam, uint8_t size) {
    return faceAt(fam, snapIndex(fam.faces, fam.count, size));
}

void releaseFamily(const Family &fam) {
    for (size_t i = 0; i < fam.count; ++i) {
        FaceSlot &slot = fam.slots[i];
        if (slot.font != nullptr && fam.faces[i].inFlash == nullptr) {
            lv_font_free(const_cast<lv_font_t *>(slot.font));
        }
        slot.font = nullptr;
        slot.attempted = false;
    }
}

const Family kValueFamily = {"jbmono",
                             "extrabold",
                             "value",
                             kValueFaces,
                             kValueCount,
                             s_valueSlots,
                             &lv_font_jbmono_medium_10_nk};

const Family kLabelFamily = {"archivo",
                             "extrabold",
                             "label",
                             kLabelFaces,
                             kLabelCount,
                             s_labelSlots,
                             &lv_font_archivo_extrabold_14_nk};

} // namespace

void FontManager::init() {
    LOG_INFO("FONT", "Font faces load on first use");
}

void FontManager::shutdown() {
    releaseFamily(kValueFamily);
    releaseFamily(kLabelFamily);
}

const lv_font_t *FontManager::value(uint8_t devicePx) {
    if (devicePx < kSmallestValuePx)
        return units();
    return resolve(kValueFamily, devicePx);
}

const lv_font_t *FontManager::units() {
    return &lv_font_jbmono_medium_10_nk;
}

const lv_font_t *FontManager::label(uint8_t size) {
    return resolve(kLabelFamily, size);
}
