// font_manager.cpp — Loads dashboard fonts from SPIFFS at boot (issue #431).
//
// Each .bin lives at `S:/fonts/orbitron_<weight>_<N>.bin` (lv_font_conv
// output). Loaded once into per-intent cached lookup tables; freed via
// shutdown(). On load failure for any size, the accessor falls back to the
// built-in `lv_font_orbitron_medium_14_nk` linked into flash (or the 32 px
// Black twin for primary sizes) so the UI always renders something readable.
//
// LVGL pool budget post-#1249 (F-1, Black 48 moved out of flash):
//   - LV_MEM_SIZE = 80 KB (lv_conf.h).
//   - SPIFFS-loaded steady-state set:
//       bold_20   ~8.3 KB · bold_24   ~11.2 KB
//       medium_10 ~2.6 KB · medium_12 ~3.6 KB · medium_16 ~5.5 KB
//       black_48  ~42.8 KB (added by F-1)
//     Total ~74 KB unpacked into pool.
//   - Remaining ~6 KB hosts widget runtime state (styles, draw descriptors).
//   - `poolHasRoomFor()` runs as pre-flight on every load — if the budget
//     ever tightens (extra glyph range, larger widget set), Black 48 is the
//     first to be skipped and primary(48) snaps down to in-flash Black 32.
//     `FONT_POOL_OOM` lands in ErrorStore so the regression is observable
//     instead of silent. Raise LV_MEM_SIZE only after measuring widget
//     allocations on hardware — past attempts at 88/96 KB broke boot.

#include "font_manager.h"
#include "diag/error_store.h"
#include "diag/logger.h"

#include <Arduino.h>
#include <SPIFFS.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <string.h>

// In-flash Orbitron Black 32 px — the symbol lives in
// src/ui/fonts/lv_font_orbitron_black_32_nk.c. 32 px stays in flash because
// the LVGL pool would not accommodate both Black sizes alongside the
// bold/medium SPIFFS loads (issue #664, PR #665). Black 48 was moved to
// SPIFFS in #1249 (F-1) — see `loadOne("black", "primary", 48, …)` below.
// The 32 px in-flash copy doubles as the snap-down fallback if the 48 px
// SPIFFS load fails (missing uploadfs, pool too tight, etc).
LV_FONT_DECLARE(lv_font_orbitron_black_32_nk);

namespace {

// Sizes shipped per tier. Driven by actual call sites (see issues #431 + #487
// + #664 + #1249):
//   primary   — gauge/timer/gear at full panel height, label_widget large
//   secondary — gauge/timer mid-band, gear mid, burn_overlay icon
//   label     — top bar text, signal headers, settings, error bar, dot/icon
//
// Restoration of the sizes dropped in PR #487 (issue #664):
//   - 32 px Black stays declared as a primary size, linked in-flash
//     (lv_font_orbitron_black_32_nk in src/ui/fonts/) instead of loaded from
//     SPIFFS.
//   - 48 px Black ships as a SPIFFS-loaded .bin (#1249 F-1, moved out of
//     flash to reclaim ~43 KB). `loadOne` runs the same `poolHasRoomFor`
//     pre-flight as the other SPIFFS sizes — if the LVGL pool cannot hold
//     the ~43 KB binary alongside the bold/medium loads, the slot stays
//     null and `resolve()` snaps primary(48) down to the 32 px in-flash
//     copy. Worst case: numerics render at 32 px instead of 48 px and a
//     `FONT_POOL_OOM` entry lands in ErrorStore — never a crash.
//   - 28 px Bold was reintroduced earlier on this branch then dropped:
//     even with both primary sizes in flash, adding the 15 KB 28 px Bold on
//     top of the existing bold/medium SPIFFS budget pushes the pool past its
//     working ceiling. 28 stays dropped (secondary text snaps to 24 px).
constexpr uint8_t kPrimarySizes[] = {32, 48};   // 32 in-flash, 48 from SPIFFS
constexpr uint8_t kSecondarySizes[] = {20, 24}; // 28 dropped — pool too tight
// 8 + 10 added for widget labels — Studio renders them at 6-9 px (#1207 follow-up,
// 2026-06-01 user feedback); the 12 px floor used to make them ~30 % wider than
// Studio. Each new bin is ~2-3 KB so the SPIFFS + LVGL-pool budget stays well
// inside its working ceiling.
constexpr uint8_t kLabelSizes[] = {8, 10, 12, 14, 16};

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

// Snaps `size` to a cached entry, returning the cached font or `fallback`
// when the slot is null. Primary callers pass the 32 px Black in-flash copy
// so primary(48) snaps to 32 px on SPIFFS load failure; secondary/label
// callers pass the 14 px Medium in-flash copy (the existing global default).
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
    LOG_INFO("FONT", "Loading font family 'orbitron'");

    // Primary tier (Black) — 32 px in-flash, 48 px from SPIFFS.
    // #1249 F-1: 48 px moved out of flash to reclaim ~43 KB; on SPIFFS load
    // failure `resolve()` snaps primary(48) down to the in-flash 32 px.
    s_primary[0] = &lv_font_orbitron_black_32_nk;
    LOG_INFO("FONT", "orbitron_black_32: using in-flash copy (saves ~20 KB pool)");
    loadOne("black", "primary", kPrimarySizes[1], s_primary[1]);

    // Secondary tier (Bold) — all sizes from SPIFFS.
    for (size_t i = 0; i < kSecondaryCount; ++i) {
        loadOne("bold", "secondary", kSecondarySizes[i], s_secondary[i]);
    }

    // Label tier (Medium) — 14 px in-flash; 10, 12, 16 from SPIFFS.
    // The 8 px bake is currently unused at any widget site, and loading it
    // pushes newlib fopen over the heap edge during FontManager::init when
    // LV_MEM_SIZE is sized to absorb the orbitron_medium_10 load. Skip the
    // 8 px load — re-enable when a widget actually requests label(8).
    s_label[0] = nullptr;
    loadOne("medium", "label", kLabelSizes[1], s_label[1]);
    loadOne("medium", "label", kLabelSizes[2], s_label[2]);
    s_label[3] = &lv_font_orbitron_medium_14_nk;
    LOG_INFO("FONT", "orbitron_medium_14: using in-flash copy (saves ~4.6 KB pool)");
    loadOne("medium", "label", kLabelSizes[4], s_label[4]);

    s_initialized = true;
}

void FontManager::shutdown() {
    auto freeAll = [](const lv_font_t **slots, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            // Skip flash-resident fonts — never pool-allocated, so never freed.
            const bool isInFlash = slots[i] == &lv_font_orbitron_medium_14_nk ||
                                   slots[i] == &lv_font_orbitron_black_32_nk;
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
    // Primary fallback is the 32 px Black in-flash copy so a Black-48 SPIFFS
    // miss still renders Black numerics (snap-down), never Medium 14.
    return resolve(kPrimarySizes, kPrimaryCount, s_primary, size, &lv_font_orbitron_black_32_nk);
}

const lv_font_t *FontManager::secondary(uint8_t size) {
    return resolve(kSecondarySizes, kSecondaryCount, s_secondary, size,
                   &lv_font_orbitron_medium_14_nk);
}

const lv_font_t *FontManager::label(uint8_t size) {
    return resolve(kLabelSizes, kLabelCount, s_label, size, &lv_font_orbitron_medium_14_nk);
}
