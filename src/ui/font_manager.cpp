// font_manager.cpp — Loads Montserrat fonts from SPIFFS at boot.
//
// Each .bin lives at `S:/fonts/montserrat_<N>.bin` (lv_font_conv output,
// regenerated via scripts/regen_montserrat_no_kern.py). Loaded once into a
// cached lookup table; freed via shutdown(). On load failure for any size,
// get() falls back to the built-in `lv_font_montserrat_14_nk` linked into
// flash so the UI always renders something readable.

#include "font_manager.h"
#include "diag/error_store.h"
#include "diag/logger.h"

#include <stdio.h>

namespace {

constexpr uint8_t kSizes[] = {12, 14, 16, 20, 24, 32, 48};
constexpr size_t  kCount   = sizeof(kSizes) / sizeof(kSizes[0]);

const lv_font_t *s_fonts[kCount] = {nullptr};
bool             s_initialized   = false;

// Returns the index of the largest kSizes[i] <= size, or 0 if none.
size_t snap_index(uint8_t size) {
    size_t idx = 0;
    for (size_t i = 0; i < kCount; ++i) {
        if (kSizes[i] <= size) {
            idx = i;
        } else {
            break;
        }
    }
    return idx;
}

} // namespace

void FontManager::init() {
    if (s_initialized) {
        return;
    }

    for (size_t i = 0; i < kCount; ++i) {
        char path[40];
        snprintf(path, sizeof(path), "S:/fonts/montserrat_%u.bin", kSizes[i]);

        const lv_font_t *font = lv_font_load(path);
        if (font == nullptr) {
            LOG_ERROR(
                "FONT",
                "Failed to load montserrat_%u.bin from SPIFFS — falling back to built-in 14",
                kSizes[i]);

            char detail[52];
            snprintf(detail, sizeof(detail), "montserrat_%u.bin missing", kSizes[i]);
            ErrorStore::push(ERROR_SRC_SYSTEM, "FONT_LOAD", detail);
        } else {
            LOG_INFO("FONT", "Loaded montserrat_%u.bin from SPIFFS", kSizes[i]);
        }
        s_fonts[i] = font;
    }

    s_initialized = true;
}

void FontManager::shutdown() {
    for (size_t i = 0; i < kCount; ++i) {
        if (s_fonts[i] != nullptr) {
            lv_font_free(const_cast<lv_font_t *>(s_fonts[i]));
            s_fonts[i] = nullptr;
        }
    }
    s_initialized = false;
}

const lv_font_t *FontManager::get(uint8_t size) {
    const size_t           idx      = snap_index(size);
    const lv_font_t *const cached   = s_fonts[idx];
    return (cached != nullptr) ? cached : &lv_font_montserrat_14_nk;
}
