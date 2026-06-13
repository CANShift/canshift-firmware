
#include "default_fonts.h"

#include "diag/error_store.h"
#include "diag/logger.h"
#include "hal/storage/storage_driver.h"

#include <Arduino.h>

extern "C" {
extern const uint8_t kFontBold20Start[] asm("_binary_data_fonts_orbitron_bold_20_bin_start");
extern const uint8_t kFontBold20End[] asm("_binary_data_fonts_orbitron_bold_20_bin_end");

extern const uint8_t kFontBold24Start[] asm("_binary_data_fonts_orbitron_bold_24_bin_start");
extern const uint8_t kFontBold24End[] asm("_binary_data_fonts_orbitron_bold_24_bin_end");

extern const uint8_t kFontMedium8Start[] asm("_binary_data_fonts_orbitron_medium_8_bin_start");
extern const uint8_t kFontMedium8End[] asm("_binary_data_fonts_orbitron_medium_8_bin_end");

extern const uint8_t kFontMedium10Start[] asm("_binary_data_fonts_orbitron_medium_10_bin_start");
extern const uint8_t kFontMedium10End[] asm("_binary_data_fonts_orbitron_medium_10_bin_end");

extern const uint8_t kFontMedium12Start[] asm("_binary_data_fonts_orbitron_medium_12_bin_start");
extern const uint8_t kFontMedium12End[] asm("_binary_data_fonts_orbitron_medium_12_bin_end");

extern const uint8_t kFontMedium14Start[] asm("_binary_data_fonts_orbitron_medium_14_bin_start");
extern const uint8_t kFontMedium14End[] asm("_binary_data_fonts_orbitron_medium_14_bin_end");

extern const uint8_t kFontMedium16Start[] asm("_binary_data_fonts_orbitron_medium_16_bin_start");
extern const uint8_t kFontMedium16End[] asm("_binary_data_fonts_orbitron_medium_16_bin_end");
}

namespace {

struct EmbeddedFont {
    const char *path;
    const uint8_t *start;
    const uint8_t *end;
    const char *label;
};

const EmbeddedFont kEmbeddedFonts[] = {
    {"/fonts/orbitron_bold_20.bin", kFontBold20Start, kFontBold20End, "orbitron_bold_20"},
    {"/fonts/orbitron_bold_24.bin", kFontBold24Start, kFontBold24End, "orbitron_bold_24"},
    {"/fonts/orbitron_medium_8.bin", kFontMedium8Start, kFontMedium8End, "orbitron_medium_8"},
    {"/fonts/orbitron_medium_10.bin", kFontMedium10Start, kFontMedium10End, "orbitron_medium_10"},
    {"/fonts/orbitron_medium_12.bin", kFontMedium12Start, kFontMedium12End, "orbitron_medium_12"},
    {"/fonts/orbitron_medium_14.bin", kFontMedium14Start, kFontMedium14End, "orbitron_medium_14"},
    {"/fonts/orbitron_medium_16.bin", kFontMedium16Start, kFontMedium16End, "orbitron_medium_16"},
};

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
             static_cast<unsigned>(result.written), static_cast<unsigned>(result.skipped),
             static_cast<unsigned>(result.failed));
    return result;
}
