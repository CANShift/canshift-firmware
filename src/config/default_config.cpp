// PlatformIO `board_build.embed_files` exposes each blob via
// `_binary_<munged_path>_start` / `_end`; we link in those symbols below.
#include "default_config.h"

#include "app_config.h"
#include "board_config.h"
#include "config/config_types.h"
#include "diag/error_store.h"
#include "diag/logger.h"
#include "hal/storage/storage_driver.h"

#include <Arduino.h>
#include <string.h>

// Mirrors writeFileAtomic's rotation suffix in config_loader.cpp.
static constexpr const char *kBakSuffix = ".bak";
static constexpr size_t kBakPathLen = CFG_MAX_PATH_LEN + 5;

// dashboard.json stays embedded — without it PageManager has 0 pages and
// the UI build crashes post-lv_init. signals.json is dropped; the flasher
// injects the per-ECU catalog (canshift-flasher#189).

extern "C" {
extern const uint8_t kDefaultDashboardStart[] asm("_binary_data_config_dashboard_json_start");
extern const uint8_t kDefaultDashboardEnd[] asm("_binary_data_config_dashboard_json_end");
}

namespace {

struct EmbeddedBlob {
    const char *path;
    const uint8_t *start;
    const uint8_t *end;
    const char *label;
};

const EmbeddedBlob kEmbedded[] = {
    {CONFIG_PATH_DASHBOARD, kDefaultDashboardStart, kDefaultDashboardEnd, "dashboard.json"},
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

// Returns true when `path` should be (re)provisioned with the embedded
// default. Preserves any user data:
//   - missing primary AND missing .bak  → provision
//   - empty primary (size == 0)         → provision
//   - anything else                     → skip
bool needsProvision(const char *path) {
    if (!StorageDriver::fileExists(path)) {
        char bakPath[kBakPathLen];
        if (!buildBakPath(bakPath, sizeof(bakPath), path)) {
            // Path too long for a .bak — fall back to "missing means provision"
            // rather than risk clobbering. CFG_MAX_PATH_LEN already bounds
            // every config path so this branch is defensive only.
            return true;
        }
        return !StorageDriver::fileExists(bakPath);
    }

    // Primary exists — only provision when it's a zero-byte placeholder.
    // Use the heap-free fileSize() probe so a 20 KB existing config doesn't
    // trigger a contiguous malloc(20 KB) on a tight boot heap (issue #576).
    return StorageDriver::fileSize(path) == 0;
}

// Write one embedded blob to its canonical path. Returns true on success.
bool writeOne(const EmbeddedBlob &blob) {
    const size_t length = static_cast<size_t>(blob.end - blob.start);
    if (length == 0) {
        LOG_ERROR("CFG", "Embedded default %s is empty — build misconfigured", blob.label);
        ErrorStore::push(ERROR_SRC_CONFIG, "EMBED_EMPTY", blob.label);
        return false;
    }
    const bool ok = StorageDriver::writeFileAtomic(blob.path, blob.start, length);
    if (!ok) {
        LOG_ERROR("CFG", "Default-provision write failed: %s", blob.path);
        ErrorStore::push(ERROR_SRC_CONFIG, "PROVISION_FAIL", blob.label);
        return false;
    }
    LOG_INFO("CFG", "Provisioned default %s (%u bytes)", blob.label, static_cast<unsigned>(length));
    return true;
}

} // namespace

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
