
#include "default_fonts.h"

#include "boot/embedded_asset.h"
#include "diag/error_store.h"
#include "diag/logger.h"
#include "hal/storage/nvs_store.h"
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
    const char *stampKey;
};

constexpr const char kStampNamespace[] = "fonts";

const EmbeddedFont kEmbeddedFonts[] = {
    {"/fonts/jbmono_extrabold_17.bin", kFontValue17Start, kFontValue17End, "jbmono_extrabold_17",
     "v17"},
    {"/fonts/jbmono_extrabold_22.bin", kFontValue22Start, kFontValue22End, "jbmono_extrabold_22",
     "v22"},
    {"/fonts/archivo_extrabold_10.bin", kFontLabel10Start, kFontLabel10End, "archivo_extrabold_10",
     "l10"},
    {"/fonts/archivo_extrabold_12.bin", kFontLabel12Start, kFontLabel12End, "archivo_extrabold_12",
     "l12"},
    {"/fonts/archivo_extrabold_16.bin", kFontLabel16Start, kFontLabel16End, "archivo_extrabold_16",
     "l16"},
};

constexpr const char *kFailureCodes[EmbeddedAsset::kOutcomeCount] = {
    nullptr, "FONT_EMPTY", "FONT_OOM", "FONT_BAD", "FONT_WRITE",
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

bool provisionedStampMatches(const EmbeddedFont &font, uint32_t embedded) {
    uint32_t stored = 0;
    return NvsStore::getULong(kStampNamespace, font.stampKey, stored) && stored == embedded;
}

bool isCurrent(const EmbeddedFont &font, uint32_t embedded) {
    if (!StorageDriver::fileExists(font.path))
        return false;
    if (provisionedStampMatches(font, embedded))
        return true;
    LOG_WARN("FONT", "%s on flash does not match this firmware's copy — rewriting", font.label);
    return false;
}

void recordStamp(const EmbeddedFont &font, uint32_t embedded) {
    if (NvsStore::putULong(kStampNamespace, font.stampKey, embedded))
        return;
    LOG_ERROR("FONT", "Could not record the stamp of %s — it is rewritten on the next boot",
              font.label);
    ErrorStore::push(ERROR_SRC_SYSTEM, "nvs_write", font.label);
}

bool provisionOne(const EmbeddedFont &font, uint32_t embedded) {
    if (!writeOne(font))
        return false;
    recordStamp(font, embedded);
    return true;
}

enum class FontState : uint8_t { Current, Written, Failed };

constexpr uint8_t DefaultFonts::ProvisionResult::*kCounters[] = {
    &DefaultFonts::ProvisionResult::skipped,
    &DefaultFonts::ProvisionResult::written,
    &DefaultFonts::ProvisionResult::failed,
};

FontState refresh(const EmbeddedFont &font) {
    uint32_t embedded = 0;
    if (!EmbeddedAsset::contentStamp(font.start, font.end, embedded)) {
        LOG_ERROR("FONT", "Embedded default %s is empty — build misconfigured", font.label);
        ErrorStore::push(ERROR_SRC_SYSTEM, "FONT_EMPTY", font.label);
        return FontState::Failed;
    }
    if (isCurrent(font, embedded))
        return FontState::Current;
    return provisionOne(font, embedded) ? FontState::Written : FontState::Failed;
}

} // namespace

DefaultFonts::ProvisionResult DefaultFonts::provisionMissingFiles() {
    ProvisionResult result = {0, 0, 0};
    for (const EmbeddedFont &font : kEmbeddedFonts)
        ++(result.*kCounters[static_cast<size_t>(refresh(font))]);
    LOG_INFO("FONT", "Fonts provisioning: %u written, %u current, %u failed",
             static_cast<unsigned>(result.written), static_cast<unsigned>(result.skipped),
             static_cast<unsigned>(result.failed));
    return result;
}
