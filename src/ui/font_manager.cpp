// font_manager.cpp — Loads Orbitron fonts from SPIFFS at boot (issue #431).
//
// Each .bin lives at `S:/fonts/orbitron_<weight>_<N>.bin` (lv_font_conv
// output). Loaded once into per-intent cached lookup tables; freed via
// shutdown(). On load failure for any size, the accessor falls back to the
// built-in `lv_font_orbitron_medium_14_nk` linked into flash so the UI always
// renders something readable.

#include "font_manager.h"
#include "diag/error_store.h"
#include "diag/logger.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <stdio.h>

namespace {

// Sizes shipped per tier. Driven by actual call sites (see issue #431):
//   primary   — gauge/timer/gear at full panel height, label_widget large
//   secondary — gauge/timer mid-band, gear mid, burn_overlay icon (28)
//   label     — top bar text, signal headers, settings, error bar, dot/icon
constexpr uint8_t kPrimarySizes[]   = {32, 48};
constexpr uint8_t kSecondarySizes[] = {20, 24, 28};
constexpr uint8_t kLabelSizes[]     = {12, 14, 16};

constexpr size_t kPrimaryCount   = sizeof(kPrimarySizes)   / sizeof(kPrimarySizes[0]);
constexpr size_t kSecondaryCount = sizeof(kSecondarySizes) / sizeof(kSecondarySizes[0]);
constexpr size_t kLabelCount     = sizeof(kLabelSizes)     / sizeof(kLabelSizes[0]);

const lv_font_t *s_primary[kPrimaryCount]     = {nullptr};
const lv_font_t *s_secondary[kSecondaryCount] = {nullptr};
const lv_font_t *s_label[kLabelCount]         = {nullptr};
bool             s_initialized                = false;

// Returns the index of the largest sizes[i] <= size, or 0 if none.
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

// Diagnostic — log free heap + largest contiguous block before/after each
// font load so the boot trace pinpoints OOM exactly when it happens (#483).
void logFontHeap(const char *stage, const char *weight, uint8_t size) {
    const uint32_t free    = ESP.getFreeHeap();
    const uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    LOG_INFO("FONT", "%s orbitron_%s_%u: free=%u largest=%u", stage, weight,
             static_cast<unsigned>(size), static_cast<unsigned>(free),
             static_cast<unsigned>(largest));
}

// Loads a single .bin into `slot` and pushes a diagnostic error on failure.
// `weight` is the file-system token used in the filename ("black", "bold",
// "medium") and `intent` is the human-readable role logged on success.
void loadOne(const char *weight, const char *intent, uint8_t size, const lv_font_t *&slot) {
    char path[48];
    snprintf(path, sizeof(path), "S:/fonts/orbitron_%s_%u.bin", weight, size);

    logFontHeap("before", weight, size);
    const lv_font_t *font = lv_font_load(path);
    logFontHeap("after ", weight, size);

    if (font == nullptr) {
        LOG_ERROR(
            "FONT",
            "Failed to load orbitron_%s_%u.bin from SPIFFS — falling back to built-in 14",
            weight, size);

        char detail[60];
        snprintf(detail, sizeof(detail), "orbitron_%s_%u.bin missing", weight, size);
        ErrorStore::push(ERROR_SRC_SYSTEM, "FONT_LOAD", detail);
    } else {
        LOG_INFO("FONT", "Loaded orbitron_%s_%u.bin from SPIFFS (%s)", weight, size, intent);
    }
    slot = font;
}

// Snaps `size` to a cached entry, returning the cached font or the in-flash
// fallback (`lv_font_orbitron_medium_14_nk`) when the slot is null.
const lv_font_t *resolve(const uint8_t *sizes, size_t count, const lv_font_t *const *cache,
                         uint8_t size) {
    const size_t           idx    = snapIndex(sizes, count, size);
    const lv_font_t *const cached = cache[idx];
    return (cached != nullptr) ? cached : &lv_font_orbitron_medium_14_nk;
}

} // namespace

void FontManager::init() {
    if (s_initialized) {
        return;
    }

    for (size_t i = 0; i < kPrimaryCount; ++i) {
        loadOne("black", "primary", kPrimarySizes[i], s_primary[i]);
    }
    for (size_t i = 0; i < kSecondaryCount; ++i) {
        loadOne("bold", "secondary", kSecondarySizes[i], s_secondary[i]);
    }
    for (size_t i = 0; i < kLabelCount; ++i) {
        loadOne("medium", "label", kLabelSizes[i], s_label[i]);
    }

    s_initialized = true;
}

void FontManager::shutdown() {
    auto freeAll = [](const lv_font_t **slots, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            if (slots[i] != nullptr) {
                lv_font_free(const_cast<lv_font_t *>(slots[i]));
                slots[i] = nullptr;
            }
        }
    };
    freeAll(s_primary, kPrimaryCount);
    freeAll(s_secondary, kSecondaryCount);
    freeAll(s_label, kLabelCount);
    s_initialized = false;
}

const lv_font_t *FontManager::primary(uint8_t size) {
    return resolve(kPrimarySizes, kPrimaryCount, s_primary, size);
}

const lv_font_t *FontManager::secondary(uint8_t size) {
    return resolve(kSecondarySizes, kSecondaryCount, s_secondary, size);
}

const lv_font_t *FontManager::label(uint8_t size) {
    return resolve(kLabelSizes, kLabelCount, s_label, size);
}
