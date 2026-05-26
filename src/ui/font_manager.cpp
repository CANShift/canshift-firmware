// font_manager.cpp — Loads dashboard fonts from SPIFFS at boot (issue #431).
//
// Each .bin lives at `S:/fonts/<family>_<weight>_<N>.bin` (lv_font_conv
// output). Loaded once into per-intent cached lookup tables; freed via
// shutdown(). On load failure for any size, the accessor falls back to the
// built-in `lv_font_orbitron_medium_14_nk` linked into flash so the UI always
// renders something readable.
//
// Family selection (issues #971 + #500): the family id arrives from
// `CfgDashboard.fontFamily` and is resolved to a `FontFamilyAssets` row by
// `resolveFamily()`. v1 catalog ships a single entry (`orbitron`) — adding a
// second family is a catalog edit plus the matching SPIFFS bundle, no
// changes to the loader, resolver, or accessors. See the docs note in
// docs/firmware/adding-a-font-family.md (#971 follow-up).

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
constexpr uint8_t kOrbitronPrimarySizes[] = {32, 48};   // both in-flash
constexpr uint8_t kOrbitronSecondarySizes[] = {20, 24}; // 28 dropped — pool too tight
constexpr uint8_t kOrbitronLabelSizes[] = {12, 14, 16};

// Maximum tier widths across every family in the catalog. The runtime cache
// arrays are sized to these constants — adding a family with a wider tier
// only needs the constant to grow, not the surrounding scaffold.
constexpr size_t kMaxPrimaryCount = 2;
constexpr size_t kMaxSecondaryCount = 2;
constexpr size_t kMaxLabelCount = 3;

static_assert(sizeof(kOrbitronPrimarySizes) / sizeof(kOrbitronPrimarySizes[0]) <= kMaxPrimaryCount,
              "orbitron primary tier exceeds kMaxPrimaryCount");
static_assert(sizeof(kOrbitronSecondarySizes) / sizeof(kOrbitronSecondarySizes[0]) <=
                  kMaxSecondaryCount,
              "orbitron secondary tier exceeds kMaxSecondaryCount");
static_assert(sizeof(kOrbitronLabelSizes) / sizeof(kOrbitronLabelSizes[0]) <= kMaxLabelCount,
              "orbitron label tier exceeds kMaxLabelCount");

// Per-family static asset bundle. Adding a family means appending a row to
// `kFamilies` (and dropping the matching `<id>_<weight>_<N>.bin` files into
// `data/fonts/`). Hot-path lookups never see this struct — it is consulted
// once inside `init()`.
struct FontFamilyAssets {
    const char *id;            // matches FontFamilyId in canshift-core
    const char *primaryWeight; // file token, e.g. "black"
    const char *secondaryWeight;
    const char *labelWeight;
    const uint8_t *primarySizes;
    size_t primaryCount;
    const uint8_t *secondarySizes;
    size_t secondaryCount;
    const uint8_t *labelSizes;
    size_t labelCount;
    // In-flash overrides keyed by px size — lookup runs once per slot at
    // init() and never on the per-character render path. `size == 0`
    // terminates the list; an empty list means "load every size from SPIFFS".
    struct InFlashOverride {
        uint8_t size;
        const lv_font_t *font;
        const char *log; // human-readable annotation for the boot log
    };
    const InFlashOverride *primaryFlash;
    const InFlashOverride *secondaryFlash;
    const InFlashOverride *labelFlash;
};

// Orbitron — v1's only family. Mirrors the single entry in FONT_FAMILIES
// in canshift-core/src/schemas/font-family.ts (issues #971 + #500).
constexpr FontFamilyAssets::InFlashOverride kOrbitronPrimaryFlash[] = {
    {32, &lv_font_orbitron_black_32_nk, "in-flash copy (saves ~20 KB pool)"},
    {48, &lv_font_orbitron_black_48_nk, "in-flash copy (saves ~44 KB pool)"},
    {0, nullptr, nullptr},
};
constexpr FontFamilyAssets::InFlashOverride kOrbitronLabelFlash[] = {
    {14, &lv_font_orbitron_medium_14_nk, "in-flash copy (saves ~4.6 KB pool)"},
    {0, nullptr, nullptr},
};

constexpr FontFamilyAssets kOrbitronAssets = {
    "orbitron",
    "black",
    "bold",
    "medium",
    kOrbitronPrimarySizes,
    sizeof(kOrbitronPrimarySizes) / sizeof(kOrbitronPrimarySizes[0]),
    kOrbitronSecondarySizes,
    sizeof(kOrbitronSecondarySizes) / sizeof(kOrbitronSecondarySizes[0]),
    kOrbitronLabelSizes,
    sizeof(kOrbitronLabelSizes) / sizeof(kOrbitronLabelSizes[0]),
    kOrbitronPrimaryFlash,
    nullptr,
    kOrbitronLabelFlash,
};

// Canonical default — mirrors DEFAULT_FONT_FAMILY_ID in canshift-core.
constexpr const FontFamilyAssets *kDefaultFamily = &kOrbitronAssets;

// Resolve a family id to its asset bundle. Unknown / empty / null inputs
// return nullptr so the caller owns the WARN + fallback log message. v1
// recognises a single id — adding a family means an extra branch here.
const FontFamilyAssets *resolveFamily(const char *family) {
    if (!family || family[0] == '\0')
        return nullptr;
    if (strcmp(family, "orbitron") == 0)
        return &kOrbitronAssets;
    return nullptr;
}

const lv_font_t *s_primary[kMaxPrimaryCount] = {nullptr};
const lv_font_t *s_secondary[kMaxSecondaryCount] = {nullptr};
const lv_font_t *s_label[kMaxLabelCount] = {nullptr};
const FontFamilyAssets *s_active_family = nullptr;
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
void loadOne(const char *familyId, const char *weight, const char *intent, uint8_t size,
             const lv_font_t *&slot) {
    char path[64];
    snprintf(path, sizeof(path), "S:/fonts/%s_%s_%u.bin", familyId, weight, size);

    // SPIFFS-side path mirrors `lvgl_fs_driver` mapping (drops the "S:" prefix).
    char spiffsPath[64];
    snprintf(spiffsPath, sizeof(spiffsPath), "/fonts/%s_%s_%u.bin", familyId, weight, size);

    logFontHeap("before", weight, size);

    const lv_font_t *font = nullptr;
    if (poolHasRoomFor(spiffsPath, weight, size)) {
        font = lv_font_load(path);
    }

    logFontHeap("after ", weight, size);

    if (font == nullptr) {
        LOG_ERROR("FONT", "Failed to load %s_%s_%u.bin from SPIFFS — falling back to built-in 14",
                  familyId, weight, size);

        char detail[60];
        snprintf(detail, sizeof(detail), "%s_%s_%u.bin missing", familyId, weight, size);
        ErrorStore::push(ERROR_SRC_SYSTEM, "FONT_LOAD", detail);
    } else {
        LOG_INFO("FONT", "Loaded %s_%s_%u.bin from SPIFFS (%s)", familyId, weight, size, intent);
    }
    slot = font;
}

// Walk the in-flash override list for a given px size. Returns the override
// row pointer (so the caller can also log the annotation), or nullptr when
// the size has no override and must be loaded from SPIFFS.
const FontFamilyAssets::InFlashOverride *
findFlashOverride(const FontFamilyAssets::InFlashOverride *list, uint8_t size) {
    if (!list)
        return nullptr;
    for (const FontFamilyAssets::InFlashOverride *row = list; row->size != 0; ++row) {
        if (row->size == size)
            return row;
    }
    return nullptr;
}

// Populate one tier of the cache (primary/secondary/label) for the given
// family. In-flash overrides win; everything else streams from SPIFFS via
// `loadOne()`.
void loadTier(const FontFamilyAssets &family, const char *weight, const char *intent,
              const uint8_t *sizes, size_t count,
              const FontFamilyAssets::InFlashOverride *flashList, const lv_font_t **slots,
              size_t slotCount) {
    for (size_t i = 0; i < count && i < slotCount; ++i) {
        const FontFamilyAssets::InFlashOverride *flash = findFlashOverride(flashList, sizes[i]);
        if (flash != nullptr) {
            slots[i] = flash->font;
            LOG_INFO("FONT", "%s_%s_%u: using %s", family.id, weight,
                     static_cast<unsigned>(sizes[i]), flash->log);
        } else {
            loadOne(family.id, weight, intent, sizes[i], slots[i]);
        }
    }
}

// Snaps `size` to a cached entry, returning the cached font or the in-flash
// fallback (`lv_font_orbitron_medium_14_nk`) when the slot is null.
const lv_font_t *resolve(const uint8_t *sizes, size_t count, const lv_font_t *const *cache,
                         uint8_t size) {
    const size_t idx = snapIndex(sizes, count, size);
    const lv_font_t *const cached = cache[idx];
    return (cached != nullptr) ? cached : &lv_font_orbitron_medium_14_nk;
}

// Populate every tier of the cache from a resolved family bundle. Runs once
// per boot — never on the per-character render path.
void applyFamily(const FontFamilyAssets &assets) {
    s_active_family = &assets;
    LOG_INFO("FONT", "Loading font family '%s'", assets.id);

    loadTier(assets, assets.primaryWeight, "primary", assets.primarySizes, assets.primaryCount,
             assets.primaryFlash, s_primary, kMaxPrimaryCount);
    loadTier(assets, assets.secondaryWeight, "secondary", assets.secondarySizes,
             assets.secondaryCount, assets.secondaryFlash, s_secondary, kMaxSecondaryCount);
    loadTier(assets, assets.labelWeight, "label", assets.labelSizes, assets.labelCount,
             assets.labelFlash, s_label, kMaxLabelCount);
}

} // namespace

void FontManager::init(const char *family) {
    if (s_initialized) {
        return;
    }
    const FontFamilyAssets *assets = resolveFamily(family);
    if (assets == nullptr) {
        // Unknown / missing id — fall back to the canonical default and keep
        // booting. Empty / null inputs (legacy dashboards pre-#1132) are
        // silent because the parser already substitutes "orbitron" by the
        // time we get here; only a hand-edited file with a typo lands a real
        // string here.
        if (family && family[0] != '\0') {
            LOG_WARN("FONT", "unknown fontFamily='%s' — falling back to default '%s'", family,
                     kDefaultFamily->id);
            char detail[48];
            snprintf(detail, sizeof(detail), "unknown family '%s'", family);
            ErrorStore::push(ERROR_SRC_SYSTEM, "FONT_FAMILY", detail);
        }
        assets = kDefaultFamily;
    }
    applyFamily(*assets);
    s_initialized = true;
}

void FontManager::init() {
    init(kDefaultFamily->id);
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
    freeAll(s_primary, kMaxPrimaryCount);
    freeAll(s_secondary, kMaxSecondaryCount);
    freeAll(s_label, kMaxLabelCount);
    s_active_family = nullptr;
    s_initialized = false;
}

const lv_font_t *FontManager::primary(uint8_t size) {
    const FontFamilyAssets *fam = s_active_family ? s_active_family : kDefaultFamily;
    return resolve(fam->primarySizes, fam->primaryCount, s_primary, size);
}

const lv_font_t *FontManager::secondary(uint8_t size) {
    const FontFamilyAssets *fam = s_active_family ? s_active_family : kDefaultFamily;
    return resolve(fam->secondarySizes, fam->secondaryCount, s_secondary, size);
}

const lv_font_t *FontManager::label(uint8_t size) {
    const FontFamilyAssets *fam = s_active_family ? s_active_family : kDefaultFamily;
    return resolve(fam->labelSizes, fam->labelCount, s_label, size);
}
