// font_manager.cpp — Loads dashboard fonts from SPIFFS at boot (issue #431).
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
#include <SPIFFS.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <string.h>

// In-flash Orbitron Black 32 + 48 px — declared here (not in lv_conf.h, which
// already exposes the 14 px Medium twin via LV_FONT_CUSTOM_DECLARE). The
// symbols live in src/ui/fonts/lv_font_orbitron_black_{32,48}_nk.c. Both
// primary sizes ship in-flash because the 80 KB LVGL pool is shared with the
// LVGL draw buffers (~25 KB) — there is no room to host the 43 KB 48 px Black
// binary in pool alongside the bold/medium SPIFFS loads (issue #664, PR #665).
LV_FONT_DECLARE(lv_font_orbitron_black_32_nk);
LV_FONT_DECLARE(lv_font_orbitron_black_48_nk);

namespace {

// Sizes shipped per tier. Driven by actual call sites (see issues #431 + #487
// + #664):
//   primary   — gauge/timer/gear at full panel height, label_widget large
//   secondary — gauge/timer mid-band, gear mid, burn_overlay icon
//   label     — top bar text, signal headers, settings, error bar, dot/icon
//
// Restoration of the sizes dropped in PR #487 (issue #664):
//   - 32 px Black stays declared as a primary size, linked in-flash
//     (lv_font_orbitron_black_32_nk in src/ui/fonts/) instead of loaded from
//     SPIFFS.
//   - 48 px Black is restored as a primary size, also linked in-flash
//     (lv_font_orbitron_black_48_nk in src/ui/fonts/). Hosting it in the LVGL
//     pool was attempted first but failed at boot: the 80 KB pool is shared
//     with the LVGL draw buffers (~25 KB) and widget runtime state, leaving
//     ~50 KB for fonts — not enough for the 43 KB 48 px binary alongside the
//     bold/medium loads. Flash linkage sidesteps the pool entirely.
//   - 28 px Bold was reintroduced earlier on this branch then dropped:
//     even with both primary sizes in flash, adding the 15 KB 28 px Bold on
//     top of the existing bold/medium SPIFFS budget pushes the pool past its
//     working ceiling. 28 stays dropped (secondary text snaps to 24 px).
constexpr uint8_t kPrimarySizes[] = {32, 48};   // both in-flash
constexpr uint8_t kSecondarySizes[] = {20, 24}; // 28 dropped — pool too tight
constexpr uint8_t kLabelSizes[] = {12, 14, 16};

constexpr size_t kPrimaryCount = sizeof(kPrimarySizes) / sizeof(kPrimarySizes[0]);
constexpr size_t kSecondaryCount = sizeof(kSecondarySizes) / sizeof(kSecondarySizes[0]);
constexpr size_t kLabelCount = sizeof(kLabelSizes) / sizeof(kLabelSizes[0]);

const lv_font_t *s_primary[kPrimaryCount] = {nullptr};
const lv_font_t *s_secondary[kSecondaryCount] = {nullptr};
const lv_font_t *s_label[kLabelCount] = {nullptr};
bool s_initialized = false;

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
    const uint32_t free = ESP.getFreeHeap();
    const uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    LOG_INFO("FONT", "%s orbitron_%s_%u: free=%u largest=%u", stage, weight,
             static_cast<unsigned>(size), static_cast<unsigned>(free),
             static_cast<unsigned>(largest));
}

// Pre-flight pool check — refuse the load when the LVGL memory pool does
// not have enough free space to hold the font's bitmap allocation. LVGL's
// `lvgl_load_font` does NOT check the return of `lv_mem_alloc`, so an OOM
// inside the loader memset's a NULL pointer and panics StoreProhibited
// (issue #557). Returning false here keeps the failure quiet, logged and
// degradable (FontManager falls back to the built-in glyph).
//
// `spiffsPath` is the path without the "S:" drive-letter prefix.
bool poolHasRoomFor(const char *spiffsPath, const char *weight, uint8_t size) {
    File f = SPIFFS.open(spiffsPath, "r");
    if (!f) {
        // Missing file — let lv_font_load fail cleanly (it does check fs_open).
        return true;
    }
    const size_t fileSize = static_cast<size_t>(f.size());
    f.close();

    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    // The font binary is fully unpacked into the LVGL pool (glyph bitmaps,
    // cmaps, glyph_dsc, glyph_offset). The bitmap blob alone is the biggest
    // single allocation and is ~60-80 % of the file size; ask for at least
    // the file size plus a small overhead to cover the other allocations.
    constexpr uint32_t kOverheadBytes = 1024;
    const uint32_t needed = static_cast<uint32_t>(fileSize) + kOverheadBytes;
    if (mon.free_size < needed) {
        LOG_ERROR("FONT",
                  "LVGL pool too small for orbitron_%s_%u.bin: need ~%u B, "
                  "have %u B free (pool=%u B). Skipping to avoid NULL-deref "
                  "inside lv_font_load.",
                  weight, static_cast<unsigned>(size), static_cast<unsigned>(needed),
                  static_cast<unsigned>(mon.free_size), static_cast<unsigned>(mon.total_size));
        char detail[60];
        snprintf(detail, sizeof(detail), "pool too small for %s_%u", weight, size);
        ErrorStore::push(ERROR_SRC_SYSTEM, "FONT_POOL_OOM", detail);
        return false;
    }
    return true;
}

// Loads a single .bin into `slot` and pushes a diagnostic error on failure.
// `weight` is the file-system token used in the filename ("black", "bold",
// "medium") and `intent` is the human-readable role logged on success.
void loadOne(const char *weight, const char *intent, uint8_t size, const lv_font_t *&slot) {
    char path[64];
    snprintf(path, sizeof(path), "S:/fonts/orbitron_%s_%u.bin", weight, size);

    // SPIFFS-side path mirrors `lvgl_fs_driver` mapping (drops the "S:" prefix).
    char spiffsPath[64];
    snprintf(spiffsPath, sizeof(spiffsPath), "/fonts/orbitron_%s_%u.bin", weight, size);

    logFontHeap("before", weight, size);

    const lv_font_t *font = nullptr;
    if (poolHasRoomFor(spiffsPath, weight, size)) {
        font = lv_font_load(path);
    }

    logFontHeap("after ", weight, size);

    if (font == nullptr) {
        LOG_ERROR("FONT",
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
    const size_t idx = snapIndex(sizes, count, size);
    const lv_font_t *const cached = cache[idx];
    return (cached != nullptr) ? cached : &lv_font_orbitron_medium_14_nk;
}

} // namespace

void FontManager::init() {
    if (s_initialized) {
        return;
    }
    LOG_INFO("FONT", "Loading font family 'orbitron'");

    // Primary tier (Black) — both sizes shipped in-flash; no SPIFFS load.
    s_primary[0] = &lv_font_orbitron_black_32_nk;
    LOG_INFO("FONT", "orbitron_black_32: using in-flash copy (saves ~20 KB pool)");
    s_primary[1] = &lv_font_orbitron_black_48_nk;
    LOG_INFO("FONT", "orbitron_black_48: using in-flash copy (saves ~44 KB pool)");

    // Secondary tier (Bold) — all sizes from SPIFFS.
    for (size_t i = 0; i < kSecondaryCount; ++i) {
        loadOne("bold", "secondary", kSecondarySizes[i], s_secondary[i]);
    }

    // Label tier (Medium) — 14 px in-flash; 12 + 16 from SPIFFS.
    s_label[0] = nullptr;
    loadOne("medium", "label", kLabelSizes[0], s_label[0]);
    s_label[1] = &lv_font_orbitron_medium_14_nk;
    LOG_INFO("FONT", "orbitron_medium_14: using in-flash copy (saves ~4.6 KB pool)");
    loadOne("medium", "label", kLabelSizes[2], s_label[2]);

    s_initialized = true;
}

void FontManager::shutdown() {
    auto freeAll = [](const lv_font_t **slots, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            // Skip flash-resident fonts — never pool-allocated, so never freed.
            const bool isInFlash = slots[i] == &lv_font_orbitron_medium_14_nk ||
                                   slots[i] == &lv_font_orbitron_black_32_nk ||
                                   slots[i] == &lv_font_orbitron_black_48_nk;
            if (slots[i] != nullptr && !isInFlash) {
                lv_font_free(const_cast<lv_font_t *>(slots[i]));
            }
            slots[i] = nullptr;
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
