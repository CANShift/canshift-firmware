#include "storage_driver.h"
#include "board_config.h"
#include "config/config_types.h"
#include "config/json_reader.h"
#include "diag/logger.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <esp_heap_caps.h>
#include <esp_partition.h>
#include <sys/stat.h>

namespace {
StorageDriver::InitStatus s_initStatus = StorageDriver::InitStatus::NotInitialized;

constexpr const char *kSpiffsMount = "/spiffs";
constexpr size_t kSpiffsMountLen = 7;
} // namespace

static constexpr size_t kAtomicSuffixLen = 4;
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

bool finalizeAtomicSwap(const char *path) {
    char tmpPath[kSuffixedPathLen];
    char bakPath[kSuffixedPathLen];
    if (!buildSuffixedPath(tmpPath, sizeof(tmpPath), path, ".tmp") ||
        !buildSuffixedPath(bakPath, sizeof(bakPath), path, ".bak")) {
        LOG_ERROR("STORAGE", "Path too long for atomic swap: %s", path);
        return false;
    }

    if (SPIFFS.exists(bakPath)) {
        if (!SPIFFS.remove(bakPath)) {
            LOG_WARN("STORAGE", "Failed to remove stale %s — proceeding", bakPath);
        }
    }

    bool hadOriginal = false;
    if (SPIFFS.exists(path)) {
        if (!SPIFFS.rename(path, bakPath)) {
            LOG_ERROR("STORAGE", "Rotate %s -> %s failed", path, bakPath);
            SPIFFS.remove(tmpPath);
            return false;
        }
        hadOriginal = true;
    }

    if (!SPIFFS.rename(tmpPath, path)) {
        LOG_ERROR("STORAGE", "Promote %s -> %s failed", tmpPath, path);

        if (hadOriginal) {
            if (!SPIFFS.rename(bakPath, path)) {
                LOG_ERROR("STORAGE", "Restore from %s failed — file lost", bakPath);
            } else {
                LOG_WARN("STORAGE", "Restored %s from .bak after promote failure", path);
            }
        }
        SPIFFS.remove(tmpPath);
        return false;
    }

    return true;
}

} // namespace

bool StorageDriver::init() {
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
    if (part != nullptr) {
        LOG_INFO("STORAGE",
                 "Backend=SPIFFS partition=spiffs offset=0x%x size=0x%x attempting mount...",
                 static_cast<unsigned>(part->address), static_cast<unsigned>(part->size));
    } else {
        LOG_INFO("STORAGE", "Backend=SPIFFS partition=spiffs offset=? size=? attempting mount...");
    }

    constexpr uint8_t kSpiffsMaxOpenFiles = 16;
    if (!SPIFFS.begin(true, kSpiffsMount, kSpiffsMaxOpenFiles)) {
        const char *reason =
            (part == nullptr) ? "partition_missing" : "partition_corrupt_or_format_fail";
        LOG_ERROR("STORAGE", "Backend=SPIFFS mount=failed reason=%s", reason);
        s_initStatus = InitStatus::MountFailed;
        return false;
    }
    size_t total, used;
    getSpaceInfo(&total, &used);
    LOG_INFO("STORAGE", "Backend=SPIFFS mount=ok total=%u used=%u free=%u",
             static_cast<unsigned>(total), static_cast<unsigned>(used),
             static_cast<unsigned>(total - used));

    sweepOrphanTmp(CONFIG_PATH_DASHBOARD);
    sweepOrphanTmp(CONFIG_PATH_SIGNALS);

    s_initStatus = InitStatus::Ok;
    return true;
}

StorageDriver::InitStatus StorageDriver::getStatus() {
    return s_initStatus;
}

DeserializationError StorageDriver::parseJsonFile(const char *path, JsonDocument &doc,
                                                  size_t *outSize) {
    if (outSize)
        *outSize = 0;
    File file = SPIFFS.open(path, "r");
    if (!file || file.isDirectory()) {
        LOG_WARN("STORAGE", "Cannot open JSON file: %s", path);
        return DeserializationError::EmptyInput;
    }
    const size_t size = file.size();
    if (outSize)
        *outSize = size;

    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err) {
        LOG_WARN("STORAGE", "Parse %s failed: %s (size=%u)", path, err.c_str(),
                 static_cast<unsigned>(size));
    } else {
        LOG_DEBUG("STORAGE", "Parsed %s (%u bytes)", path, static_cast<unsigned>(size));
    }
    return err;
}

size_t StorageDriver::fileSize(const char *path) {
    File file = SPIFFS.open(path, "r");
    if (!file || file.isDirectory())
        return 0;
    const size_t size = file.size();
    file.close();
    return size;
}

size_t StorageDriver::streamFileTo(const char *path, Print &out, bool replaceNewlinesWithSpaces) {
    File file = SPIFFS.open(path, "r");
    if (!file || file.isDirectory()) {
        LOG_WARN("STORAGE", "Cannot open file for streaming: %s", path);
        return 0;
    }
    constexpr size_t kStreamChunk = 256;
    uint8_t buf[kStreamChunk];
    size_t total = 0;
    while (file.available() > 0) {
        const int n = file.read(buf, sizeof(buf));
        if (n <= 0)
            break;
        if (replaceNewlinesWithSpaces) {
            for (int i = 0; i < n; ++i) {
                if (buf[i] == '\n' || buf[i] == '\r')
                    buf[i] = ' ';
            }
        }
        out.write(buf, static_cast<size_t>(n));
        total += static_cast<size_t>(n);
    }
    file.close();
    return total;
}

bool StorageDriver::writeFileAtomic(const char *path, const uint8_t *data, size_t length) {
    char tmpPath[kSuffixedPathLen];
    if (!buildSuffixedPath(tmpPath, sizeof(tmpPath), path, ".tmp")) {
        LOG_ERROR("STORAGE", "Path too long for atomic write: %s", path);
        return false;
    }

    if (SPIFFS.exists(tmpPath)) {
        SPIFFS.remove(tmpPath);
    }

    File file = SPIFFS.open(tmpPath, "w");
    if (!file) {
        LOG_ERROR("STORAGE", "Cannot open for atomic write: %s", tmpPath);
        return false;
    }

    const size_t written = file.write(data, length);
    file.close();

    if (written != length) {
        LOG_ERROR("STORAGE", "Atomic write incomplete: %u/%u bytes for %s", written, length, path);
        SPIFFS.remove(tmpPath);
        return false;
    }

    if (!finalizeAtomicSwap(path)) {
        return false;
    }

    LOG_DEBUG("STORAGE", "Atomically wrote %u bytes to %s", written, path);
    return true;
}

bool StorageDriver::fileExists(const char *path) {

    if (!path || path[0] != '/')
        return false;
    char full[kSpiffsMountLen + CFG_MAX_PATH_LEN + 1];
    const int n = snprintf(full, sizeof(full), "%s%s", kSpiffsMount, path);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(full))
        return false;
    struct stat st;
    return ::stat(full, &st) == 0;
}

bool StorageDriver::renameFile(const char *src, const char *dst) {
    return SPIFFS.rename(src, dst);
}

bool StorageDriver::removeFile(const char *path) {
    if (!SPIFFS.exists(path))
        return true;
    return SPIFFS.remove(path);
}

void StorageDriver::getSpaceInfo(size_t *totalBytes, size_t *usedBytes) {
    if (totalBytes)
        *totalBytes = SPIFFS.totalBytes();
    if (usedBytes)
        *usedBytes = SPIFFS.usedBytes();
}

void StorageDriver::sweepOrphanTmp(const char *path) {
    char tmpPath[kSuffixedPathLen];
    if (!buildSuffixedPath(tmpPath, sizeof(tmpPath), path, ".tmp"))
        return;
    if (!SPIFFS.exists(tmpPath))
        return;
    if (SPIFFS.remove(tmpPath)) {
        LOG_WARN("STORAGE", "Removed orphan %s (power-cut recovery)", tmpPath);
    } else {
        LOG_WARN("STORAGE", "Failed to remove orphan %s", tmpPath);
    }
}

namespace {
File s_chunkFile;
bool s_chunkOpen = false;
bool s_chunkAtomic = false;
char s_chunkAtomicPath[CFG_MAX_PATH_LEN] = {0};
char s_chunkActualPath[kSuffixedPathLen] = {0};
} // namespace

bool StorageDriver::ensureParentDirs(const char *path) {
    if (!path || path[0] != '/')
        return false;

    char buf[128];
    strlcpy(buf, path, sizeof(buf));

    char *cursor = buf + 1;
    while (true) {
        char *slash = strchr(cursor, '/');
        if (!slash)
            break;
        *slash = '\0';
        if (!SPIFFS.exists(buf)) {
            if (!SPIFFS.mkdir(buf)) {
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

    if (SPIFFS.exists(tmpPath)) {
        SPIFFS.remove(tmpPath);
    }

    s_chunkFile = SPIFFS.open(tmpPath, "w");
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
        SPIFFS.remove(s_chunkActualPath);
    }
    s_chunkAtomic = false;
    s_chunkAtomicPath[0] = '\0';
    s_chunkActualPath[0] = '\0';
}

bool StorageDriver::isChunkedWriteOpen() {
    return s_chunkOpen;
}
