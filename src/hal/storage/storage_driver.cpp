// storage_driver.cpp — Filesystem abstraction implementation

#include "storage_driver.h"
#include "board_config.h"
#include "config/config_types.h" // CFG_MAX_PATH_LEN
#include "diag/logger.h"

#include <Arduino.h>

#if STORAGE_USE_SPIFFS
    #include <SPIFFS.h>
    #define FS_INSTANCE SPIFFS
#elif STORAGE_USE_SD
    #include <SD.h>
    #define FS_INSTANCE SD
#else
    #error "No storage backend selected in board_config.h"
#endif

// Suffix length: ".tmp" / ".bak" = 4 chars + null terminator.
static constexpr size_t kAtomicSuffixLen = 4;
// Buffer holding "<path>" + suffix + '\0'. Bounded to refuse paths that don't
// fit in the storage driver's path budget.
static constexpr size_t kSuffixedPathLen = CFG_MAX_PATH_LEN + kAtomicSuffixLen + 1;

namespace {

bool buildSuffixedPath(char *out, size_t outLen, const char *base, const char *suffix) {
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

// Promote "<path>.tmp" to "<path>", rotating the previous "<path>" through
// "<path>.bak". On rename failure mid-rotation the previous file is restored
// from the .bak when possible. Returns true on success.
bool finalizeAtomicSwap(const char *path) {
    char tmpPath[kSuffixedPathLen];
    char bakPath[kSuffixedPathLen];
    if (!buildSuffixedPath(tmpPath, sizeof(tmpPath), path, ".tmp") ||
        !buildSuffixedPath(bakPath, sizeof(bakPath), path, ".bak")) {
        LOG_ERROR("STORAGE", "Path too long for atomic swap: %s", path);
        return false;
    }

    // Drop any stale .bak from a previous rotation.
    if (FS_INSTANCE.exists(bakPath)) {
        if (!FS_INSTANCE.remove(bakPath)) {
            LOG_WARN("STORAGE", "Failed to remove stale %s — proceeding", bakPath);
        }
    }

    // Rotate the live file aside, if it exists (first-install case is fine).
    bool hadOriginal = false;
    if (FS_INSTANCE.exists(path)) {
        if (!FS_INSTANCE.rename(path, bakPath)) {
            LOG_ERROR("STORAGE", "Rotate %s -> %s failed", path, bakPath);
            // Drop the staged .tmp so we don't leak it.
            FS_INSTANCE.remove(tmpPath);
            return false;
        }
        hadOriginal = true;
    }

    // Promote the staged .tmp into place.
    if (!FS_INSTANCE.rename(tmpPath, path)) {
        LOG_ERROR("STORAGE", "Promote %s -> %s failed", tmpPath, path);
        // Best-effort restore so the device still boots with the prior config.
        if (hadOriginal) {
            if (!FS_INSTANCE.rename(bakPath, path)) {
                LOG_ERROR("STORAGE", "Restore from %s failed — file lost", bakPath);
            } else {
                LOG_WARN("STORAGE", "Restored %s from .bak after promote failure", path);
            }
        }
        FS_INSTANCE.remove(tmpPath);
        return false;
    }

    return true;
}

} // namespace

bool StorageDriver::init() {
#if STORAGE_USE_SPIFFS
    if (!SPIFFS.begin(true /* formatOnFail */)) {
        LOG_ERROR("STORAGE", "SPIFFS mount failed");
        return false;
    }
    LOG_INFO("STORAGE", "SPIFFS mounted");
    size_t total, used;
    getSpaceInfo(&total, &used);
    LOG_INFO("STORAGE", "SPIFFS: %u bytes total, %u used", total, used);

#elif STORAGE_USE_SD
    if (!SD.begin(PIN_SD_CS)) {
        LOG_ERROR("STORAGE", "SD card mount failed");
        return false;
    }
    LOG_INFO("STORAGE", "SD card mounted");
#endif

    // Sweep orphan .tmp files left behind by a power-cut mid-write so the
    // next atomic rotation starts from a clean slate.
    sweepOrphanTmp(CONFIG_PATH_DASHBOARD);
    sweepOrphanTmp(CONFIG_PATH_SIGNALS);
    sweepOrphanTmp(CONFIG_PATH_DEVICE);
    return true;
}

char *StorageDriver::readFile(const char *path, size_t *outSize) {
    File file = FS_INSTANCE.open(path, "r");
    if (!file || file.isDirectory()) {
        LOG_WARN("STORAGE", "Cannot open file: %s", path);
        if (outSize)
            *outSize = 0;
        return nullptr;
    }

    size_t size = file.size();
    char *buf = static_cast<char *>(malloc(size + 1));
    if (!buf) {
        LOG_ERROR("STORAGE", "malloc(%u) failed reading %s", size + 1, path);
        file.close();
        if (outSize)
            *outSize = 0;
        return nullptr;
    }

    size_t read = file.readBytes(buf, size);
    buf[read] = '\0';
    file.close();

    if (outSize)
        *outSize = read;
    LOG_DEBUG("STORAGE", "Read %u bytes from %s", read, path);
    return buf;
}

bool StorageDriver::writeFile(const char *path, const uint8_t *data, size_t length) {
    File file = FS_INSTANCE.open(path, "w");
    if (!file) {
        LOG_ERROR("STORAGE", "Cannot open for write: %s", path);
        return false;
    }

    size_t written = file.write(data, length);
    file.close();

    if (written != length) {
        LOG_ERROR("STORAGE", "Write incomplete: %u/%u bytes for %s", written, length, path);
        return false;
    }

    LOG_DEBUG("STORAGE", "Wrote %u bytes to %s", written, path);
    return true;
}

bool StorageDriver::writeFileAtomic(const char *path, const uint8_t *data, size_t length) {
    char tmpPath[kSuffixedPathLen];
    if (!buildSuffixedPath(tmpPath, sizeof(tmpPath), path, ".tmp")) {
        LOG_ERROR("STORAGE", "Path too long for atomic write: %s", path);
        return false;
    }

    // If a previous attempt left a stale .tmp behind, drop it before retrying.
    if (FS_INSTANCE.exists(tmpPath)) {
        FS_INSTANCE.remove(tmpPath);
    }

    File file = FS_INSTANCE.open(tmpPath, "w");
    if (!file) {
        LOG_ERROR("STORAGE", "Cannot open for atomic write: %s", tmpPath);
        return false;
    }

    const size_t written = file.write(data, length);
    file.close();

    if (written != length) {
        LOG_ERROR("STORAGE", "Atomic write incomplete: %u/%u bytes for %s", written, length, path);
        FS_INSTANCE.remove(tmpPath);
        return false;
    }

    if (!finalizeAtomicSwap(path)) {
        return false;
    }

    LOG_DEBUG("STORAGE", "Atomically wrote %u bytes to %s", written, path);
    return true;
}

bool StorageDriver::fileExists(const char *path) {
    return FS_INSTANCE.exists(path);
}

bool StorageDriver::renameFile(const char *src, const char *dst) {
    return FS_INSTANCE.rename(src, dst);
}

bool StorageDriver::removeFile(const char *path) {
    if (!FS_INSTANCE.exists(path))
        return true;
    return FS_INSTANCE.remove(path);
}

void StorageDriver::getSpaceInfo(size_t *totalBytes, size_t *usedBytes) {
#if STORAGE_USE_SPIFFS
    if (totalBytes)
        *totalBytes = SPIFFS.totalBytes();
    if (usedBytes)
        *usedBytes = SPIFFS.usedBytes();
#elif STORAGE_USE_SD
    if (totalBytes)
        *totalBytes = SD.totalBytes();
    if (usedBytes)
        *usedBytes = SD.usedBytes();
#endif
}

void StorageDriver::sweepOrphanTmp(const char *path) {
    char tmpPath[kSuffixedPathLen];
    if (!buildSuffixedPath(tmpPath, sizeof(tmpPath), path, ".tmp"))
        return;
    if (!FS_INSTANCE.exists(tmpPath))
        return;
    if (FS_INSTANCE.remove(tmpPath)) {
        LOG_WARN("STORAGE", "Removed orphan %s (power-cut recovery)", tmpPath);
    } else {
        LOG_WARN("STORAGE", "Failed to remove orphan %s", tmpPath);
    }
}

// ---------------------------------------------------------------------------
// Chunked write
// ---------------------------------------------------------------------------

namespace {
File s_chunkFile;
bool s_chunkOpen = false;
bool s_chunkAtomic = false;
char s_chunkAtomicPath[CFG_MAX_PATH_LEN] = {0};
char s_chunkActualPath[kSuffixedPathLen] = {0}; // path actually opened (with .tmp if atomic)
} // namespace

bool StorageDriver::ensureParentDirs(const char *path) {
    if (!path || path[0] != '/')
        return false;

    // Walk slashes, mkdir each prefix. Skip the final segment (the file name).
    char buf[128];
    strlcpy(buf, path, sizeof(buf));

    char *cursor = buf + 1; // skip leading '/'
    while (true) {
        char *slash = strchr(cursor, '/');
        if (!slash)
            break;
        *slash = '\0';
        if (!FS_INSTANCE.exists(buf)) {
            if (!FS_INSTANCE.mkdir(buf)) {
                LOG_WARN("STORAGE", "mkdir failed: %s", buf);
                *slash = '/';
                return false;
            }
        }
        *slash = '/';
        cursor = slash + 1;
    }
    return true;
}

bool StorageDriver::beginChunkedWrite(const char *path) {
    if (s_chunkOpen) {
        LOG_WARN("STORAGE", "Replacing in-flight chunked write");
        abortChunkedWrite();
    }

    if (!ensureParentDirs(path)) {
        return false;
    }

    s_chunkFile = FS_INSTANCE.open(path, "w");
    if (!s_chunkFile) {
        LOG_ERROR("STORAGE", "Open for chunked write failed: %s", path);
        return false;
    }
    s_chunkOpen = true;
    s_chunkAtomic = false;
    s_chunkAtomicPath[0] = '\0';
    strlcpy(s_chunkActualPath, path, sizeof(s_chunkActualPath));
    return true;
}

bool StorageDriver::beginChunkedWriteAtomic(const char *path) {
    if (s_chunkOpen) {
        LOG_WARN("STORAGE", "Replacing in-flight chunked write");
        abortChunkedWrite();
    }

    if (!path || strlen(path) >= CFG_MAX_PATH_LEN) {
        LOG_ERROR("STORAGE", "Path too long for atomic chunked write");
        return false;
    }

    if (!ensureParentDirs(path)) {
        return false;
    }

    char tmpPath[kSuffixedPathLen];
    if (!buildSuffixedPath(tmpPath, sizeof(tmpPath), path, ".tmp")) {
        return false;
    }

    if (FS_INSTANCE.exists(tmpPath)) {
        FS_INSTANCE.remove(tmpPath);
    }

    s_chunkFile = FS_INSTANCE.open(tmpPath, "w");
    if (!s_chunkFile) {
        LOG_ERROR("STORAGE", "Open for atomic chunked write failed: %s", tmpPath);
        return false;
    }
    s_chunkOpen = true;
    s_chunkAtomic = true;
    strlcpy(s_chunkAtomicPath, path, sizeof(s_chunkAtomicPath));
    strlcpy(s_chunkActualPath, tmpPath, sizeof(s_chunkActualPath));
    return true;
}

bool StorageDriver::appendChunk(const uint8_t *data, size_t length) {
    if (!s_chunkOpen)
        return false;
    size_t written = s_chunkFile.write(data, length);
    if (written != length) {
        LOG_ERROR("STORAGE", "Chunk write incomplete: %u/%u", written, length);
        return false;
    }
    return true;
}

bool StorageDriver::endChunkedWrite() {
    if (!s_chunkOpen)
        return true;

    s_chunkFile.close();
    s_chunkOpen = false;

    bool ok = true;
    if (s_chunkAtomic) {
        ok = finalizeAtomicSwap(s_chunkAtomicPath);
        if (!ok) {
            // finalizeAtomicSwap already logged + cleaned up the .tmp.
            LOG_ERROR("STORAGE", "Atomic chunked finalize failed: %s", s_chunkAtomicPath);
        }
    }

    s_chunkAtomic = false;
    s_chunkAtomicPath[0] = '\0';
    s_chunkActualPath[0] = '\0';
    return ok;
}

void StorageDriver::abortChunkedWrite() {
    if (s_chunkOpen) {
        s_chunkFile.close();
        s_chunkOpen = false;
    }
    if (s_chunkAtomic && s_chunkActualPath[0] != '\0') {
        FS_INSTANCE.remove(s_chunkActualPath);
    }
    s_chunkAtomic = false;
    s_chunkAtomicPath[0] = '\0';
    s_chunkActualPath[0] = '\0';
}

bool StorageDriver::isChunkedWriteOpen() {
    return s_chunkOpen;
}
