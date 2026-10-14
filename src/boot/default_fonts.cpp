
#include "default_fonts.h"

#include "boot/embedded_asset.h"
#include "diag/error_store.h"
#include "diag/logger.h"
#include "hal/storage/storage_driver.h"

#include <Arduino.h>

extern "C" {
extern const uint8_t
    kFontValue17Start[] asm("_binary__pio_deflated_data_fonts_jbmono_extrabold_17_bin_start");
extern const uint8_t
    kFontValue17End[] asm("_binary__pio_deflated_data_fonts_jbmono_extrabold_17_bin_end");

extern const uint8_t
    kFontValue22Start[] asm("_binary__pio_deflated_data_fonts_jbmono_extrabold_22_bin_start");
extern const uint8_t
    kFontValue22End[] asm("_binary__pio_deflated_data_fonts_jbmono_extrabold_22_bin_end");

extern const uint8_t
    kFontLabel10Start[] asm("_binary__pio_deflated_data_fonts_archivo_extrabold_10_bin_start");
extern const uint8_t
    kFontLabel10End[] asm("_binary__pio_deflated_data_fonts_archivo_extrabold_10_bin_end");

extern const uint8_t
    kFontLabel12Start[] asm("_binary__pio_deflated_data_fonts_archivo_extrabold_12_bin_start");
extern const uint8_t
    kFontLabel12End[] asm("_binary__pio_deflated_data_fonts_archivo_extrabold_12_bin_end");

extern const uint8_t
    kFontLabel16Start[] asm("_binary__pio_deflated_data_fonts_archivo_extrabold_16_bin_start");
extern const uint8_t
    kFontLabel16End[] asm("_binary__pio_deflated_data_fonts_archivo_extrabold_16_bin_end");
}

namespace {

struct EmbeddedFont {
    const char *path;
    const uint8_t *start;
    const uint8_t *end;
    const char *label;
};

const EmbeddedFont kEmbeddedFonts[] = {
    {"/fonts/jbmono_extrabold_17.bin", kFontValue17Start, kFontValue17End, "jbmono_extrabold_17"},
    {"/fonts/jbmono_extrabold_22.bin", kFontValue22Start, kFontValue22End, "jbmono_extrabold_22"},
    {"/fonts/archivo_extrabold_10.bin", kFontLabel10Start, kFontLabel10End, "archivo_extrabold_10"},
    {"/fonts/archivo_extrabold_12.bin", kFontLabel12Start, kFontLabel12End, "archivo_extrabold_12"},
    {"/fonts/archivo_extrabold_16.bin", kFontLabel16Start, kFontLabel16End, "archivo_extrabold_16"},
};

constexpr const char *kFailureCodes[EmbeddedAsset::kOutcomeCount] = {
    nullptr,
    "FONT_EMBED_EMPTY",
    "FONT_EMBED_NO_MEMORY",
    "FONT_EMBED_CORRUPT",
    "FONT_PROVISION_FAIL",
};

bool writeOne(const EmbeddedFont &font) {
    size_t length = 0;
    const EmbeddedAsset::Outcome outcome =
        EmbeddedAsset::inflateToFile(font.path, font.start, font.end, length);
    if (outcome != EmbeddedAsset::Outcome::Written) {
        const char *code = kFailureCodes[static_cast<size_t>(outcome)];
        LOG_ERROR("FONT", "Default-font provision of %s failed: %s", font.label, code);
        ErrorStore::push(ERROR_SRC_SYSTEM, code, font.label);
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
