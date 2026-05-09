// default_fonts.cpp — Embed + write baked-in Orbitron .bin fonts on first boot.
//
// Symbol naming: PlatformIO's embed_files generates `_binary_<munged_path>`
// where the munged path is the file path with non-identifier characters
// replaced by underscores. Embed sources live under `data/fonts/` (see
// platformio.ini), giving e.g. `_binary_data_fonts_orbitron_black_32_bin_start`.
//
// Font payloads are linked in via PlatformIO `board_build.embed_files`. Each
// embedded blob exposes `_binary_<munged_path>_start` / `_end` symbols. We
// reference them with `extern "C"` declarations and let the linker fill in
// the addresses (see https://docs.platformio.org/en/latest/platforms/espressif32.html).
//
// Mirror of src/config/default_config.cpp — same provisioning pattern, applied
// to the 8 SPIFFS-resident font files shipped by FontManager (issue #467).

#include "default_fonts.h"

#include "diag/error_store.h"
#include "diag/logger.h"
#include "hal/storage/storage_driver.h"

#include <Arduino.h>

extern "C" {
extern const uint8_t kFontBlack32Start[] asm("_binary_data_fonts_orbitron_black_32_bin_start");
extern const uint8_t kFontBlack32End[] asm("_binary_data_fonts_orbitron_black_32_bin_end");

extern const uint8_t kFontBlack48Start[] asm("_binary_data_fonts_orbitron_black_48_bin_start");
extern const uint8_t kFontBlack48End[] asm("_binary_data_fonts_orbitron_black_48_bin_end");

extern const uint8_t kFontBold20Start[] asm("_binary_data_fonts_orbitron_bold_20_bin_start");
extern const uint8_t kFontBold20End[] asm("_binary_data_fonts_orbitron_bold_20_bin_end");

extern const uint8_t kFontBold24Start[] asm("_binary_data_fonts_orbitron_bold_24_bin_start");
extern const uint8_t kFontBold24End[] asm("_binary_data_fonts_orbitron_bold_24_bin_end");

extern const uint8_t kFontBold28Start[] asm("_binary_data_fonts_orbitron_bold_28_bin_start");
extern const uint8_t kFontBold28End[] asm("_binary_data_fonts_orbitron_bold_28_bin_end");

extern const uint8_t kFontMedium12Start[] asm("_binary_data_fonts_orbitron_medium_12_bin_start");
extern const uint8_t kFontMedium12End[] asm("_binary_data_fonts_orbitron_medium_12_bin_end");

extern const uint8_t kFontMedium14Start[] asm("_binary_data_fonts_orbitron_medium_14_bin_start");
extern const uint8_t kFontMedium14End[] asm("_binary_data_fonts_orbitron_medium_14_bin_end");

extern const uint8_t kFontMedium16Start[] asm("_binary_data_fonts_orbitron_medium_16_bin_start");
extern const uint8_t kFontMedium16End[] asm("_binary_data_fonts_orbitron_medium_16_bin_end");
}

namespace {

struct EmbeddedFont {
    const char *path;  // Canonical SPIFFS path (StorageDriver expects no "S:" prefix)
    const uint8_t *start;
    const uint8_t *end;
    const char *label; // Short human-readable name for logs
};

const EmbeddedFont kEmbeddedFonts[] = {
    {"/fonts/orbitron_black_32.bin",   kFontBlack32Start,  kFontBlack32End,  "orbitron_black_32"},
    {"/fonts/orbitron_black_48.bin",   kFontBlack48Start,  kFontBlack48End,  "orbitron_black_48"},
    {"/fonts/orbitron_bold_20.bin",    kFontBold20Start,   kFontBold20End,   "orbitron_bold_20"},
    {"/fonts/orbitron_bold_24.bin",    kFontBold24Start,   kFontBold24End,   "orbitron_bold_24"},
    {"/fonts/orbitron_bold_28.bin",    kFontBold28Start,   kFontBold28End,   "orbitron_bold_28"},
    {"/fonts/orbitron_medium_12.bin",  kFontMedium12Start, kFontMedium12End, "orbitron_medium_12"},
    {"/fonts/orbitron_medium_14.bin",  kFontMedium14Start, kFontMedium14End, "orbitron_medium_14"},
    {"/fonts/orbitron_medium_16.bin",  kFontMedium16Start, kFontMedium16End, "orbitron_medium_16"},
};

// Write one embedded blob to its canonical path. Returns true on success.
bool writeOne(const EmbeddedFont &font) {
    const size_t length = static_cast<size_t>(font.end - font.start);
    if (length == 0) {
        LOG_ERROR("FONT", "Embedded default %s is empty — build misconfigured", font.label);
        ErrorStore::push(ERROR_SRC_SYSTEM, "FONT_EMBED_EMPTY", font.label);
        return false;
    }
    const bool ok = StorageDriver::writeFileAtomic(font.path, font.start, length);
    if (!ok) {
        LOG_ERROR("FONT", "Default-font provision write failed: %s", font.path);
        ErrorStore::push(ERROR_SRC_SYSTEM, "FONT_PROVISION_FAIL", font.label);
        return false;
    }
    LOG_INFO("FONT", "Provisioned default %s (%u bytes)", font.label,
             static_cast<unsigned>(length));
    return true;
}

} // namespace

DefaultFonts::ProvisionResult DefaultFonts::provisionMissingFiles() {
    ProvisionResult result = {0, 0, 0};
    for (const EmbeddedFont &font : kEmbeddedFonts) {
        if (StorageDriver::fileExists(font.path)) {
            ++result.skipped;
            continue;
        }
        if (writeOne(font)) {
            ++result.written;
        } else {
            ++result.failed;
        }
    }
    LOG_INFO("FONT", "Fonts provisioning: %u new, %u existing, %u failed",
             static_cast<unsigned>(result.written),
             static_cast<unsigned>(result.skipped),
             static_cast<unsigned>(result.failed));
    return result;
}
