#include "storage_atomic_swap.h"
#include "diag/logger.h"

#include <SPIFFS.h>
#include <string.h>

namespace {

void dropStaleBackup(const char *bakPath) {
    if (SPIFFS.exists(bakPath) && !SPIFFS.remove(bakPath))
        LOG_WARN("STORAGE", "Failed to remove stale %s — proceeding", bakPath);
}

void restoreOriginal(const char *bakPath, const char *path) {
    if (!SPIFFS.rename(bakPath, path)) {
        LOG_ERROR("STORAGE", "Restore from %s failed — file lost", bakPath);
        return;
    }
    LOG_WARN("STORAGE", "Restored %s from .bak after promote failure", path);
}

bool promoteTmp(const char *tmpPath, const char *path, const char *bakPath, bool hadOriginal) {
    if (SPIFFS.rename(tmpPath, path))
        return true;
    LOG_ERROR("STORAGE", "Promote %s -> %s failed", tmpPath, path);
    if (hadOriginal)
        restoreOriginal(bakPath, path);
    SPIFFS.remove(tmpPath);
    return false;
}

}

bool StorageAtomicSwap::buildSuffixedPath(char *out, size_t outLen, const char *base,
                                          const char *suffix) {
    if (!out || !base || !suffix || outLen == 0)
        return false;
    const size_t baseLen = strlen(base);
    const size_t suffixLen = strlen(suffix);
    if (baseLen + suffixLen + 1 > outLen)
        return false;
    memcpy(out, base, baseLen);
    memcpy(out + baseLen, suffix, suffixLen);
    out[baseLen + suffixLen] = '\0';
    return true;
}

bool StorageAtomicSwap::finalize(const char *path) {
    char tmpPath[kSuffixedPathLen];
    char bakPath[kSuffixedPathLen];
    if (!buildSuffixedPath(tmpPath, sizeof(tmpPath), path, ".tmp") ||
        !buildSuffixedPath(bakPath, sizeof(bakPath), path, ".bak")) {
        LOG_ERROR("STORAGE", "Path too long for atomic swap: %s", path);
        return false;
    }

    dropStaleBackup(bakPath);
    const bool hadOriginal = SPIFFS.exists(path);
    if (hadOriginal && !SPIFFS.rename(path, bakPath)) {
        LOG_ERROR("STORAGE", "Rotate %s -> %s failed", path, bakPath);
        SPIFFS.remove(tmpPath);
        return false;
    }
    return promoteTmp(tmpPath, path, bakPath, hadOriginal);
}
