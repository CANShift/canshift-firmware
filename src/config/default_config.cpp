
#include "default_config.h"

#include "app_config.h"
#include "board_config.h"
#include "boot/embedded_asset.h"
#include "config/config_types.h"
#include "diag/error_store.h"
#include "diag/logger.h"
#include "hal/storage/storage_driver.h"

#include <Arduino.h>
#include <string.h>

static constexpr const char *kBakSuffix = ".bak";
static constexpr size_t kBakPathLen = kCfgMaxPathLen + 5;

extern "C" {
extern const uint8_t
    kDefaultDashboardStart[] asm("_binary__pio_deflated_data_config_dashboard_json_start");
extern const uint8_t
    kDefaultDashboardEnd[] asm("_binary__pio_deflated_data_config_dashboard_json_end");
}

namespace {

struct EmbeddedBlob {
    const char *path;
    const uint8_t *start;
    const uint8_t *end;
    const char *label;
};

const EmbeddedBlob kEmbedded[] = {
    {kConfigPathDashboard, kDefaultDashboardStart, kDefaultDashboardEnd, "dashboard.json"},
};

bool buildBakPath(char *out, size_t outLen, const char *base) {
    if (!out || !base || outLen == 0)
        return false;
    const size_t baseLen = strlen(base);
    const size_t suffixLen = strlen(kBakSuffix);
    if (baseLen + suffixLen + 1 > outLen)
        return false;
    memcpy(out, base, baseLen);
    memcpy(out + baseLen, kBakSuffix, suffixLen);
    out[baseLen + suffixLen] = '\0';
    return true;
}

bool needsProvision(const char *path) {
    if (!StorageDriver::fileExists(path)) {
        char bakPath[kBakPathLen];
        if (!buildBakPath(bakPath, sizeof(bakPath), path)) {
            return true;
        }
        return !StorageDriver::fileExists(bakPath);
    }

    return StorageDriver::fileSize(path) == 0;
}

constexpr const char *kFailureCodes[EmbeddedAsset::kOutcomeCount] = {
    nullptr, "EMBED_EMPTY", "EMBED_OOM", "EMBED_BAD", "EMBED_WRITE",
};

bool writeOne(const EmbeddedBlob &blob) {
    size_t length = 0;
    const EmbeddedAsset::Outcome outcome =
        EmbeddedAsset::inflateToFile(blob.path, blob.start, blob.end, length);
    if (outcome != EmbeddedAsset::Outcome::Written) {
        const char *code = kFailureCodes[static_cast<size_t>(outcome)];
        LOG_ERROR("CFG", "Default-provision of %s failed: %s", blob.label, code);
        ErrorStore::push(ERROR_SRC_CONFIG, code, blob.label);
        return false;
    }
    LOG_INFO("CFG", "Provisioned default %s (%u bytes)", blob.label, static_cast<unsigned>(length));
    return true;
}

}

DefaultConfig::ProvisionResult DefaultConfig::provisionMissingFiles() {
    ProvisionResult result = {0, 0, 0};
    for (const EmbeddedBlob &blob : kEmbedded) {
        if (!needsProvision(blob.path)) {
            ++result.skipped;
            continue;
        }
        if (writeOne(blob)) {
            ++result.written;
        } else {
            ++result.failed;
        }
    }
    return result;
}
