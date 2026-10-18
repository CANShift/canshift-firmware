
#include "config_loader_internal.h"

#include "app_config.h"
#include "diag/logger.h"
#include "hal/storage/storage_driver.h"

#include <ArduinoJson.h>
#include <cstdlib>
#include <cstring>

#ifdef ARDUINO
    #include <esp_heap_caps.h>
#endif

namespace ConfigLoaderInternal {

namespace {
constexpr const char *kBakSuffix = ".bak";

const char *s_lastParseError = "file missing";
DeserializationError::Code s_lastParseCode = DeserializationError::Ok;

constexpr size_t kBakPathLen = kCfgMaxPathLen + 5;

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

void logPrimaryParseFailure(const char *path, const DeserializationError &err) {
    if (err == DeserializationError::EmptyInput) {
        LOG_WARN("CFG", "%s could not be opened — falling back to .bak", path);
        return;
    }
    LOG_WARN("CFG", "%s parse error: %s — falling back to .bak", path, err.c_str());
}

bool parsePrimary(const char *path, JsonDocument &doc) {
    if (!StorageDriver::fileExists(path)) {
        LOG_WARN("CFG", "%s missing — falling back to .bak", path);
        return false;
    }
    DeserializationError err = StorageDriver::parseJsonFile(path, doc);
    if (!err)
        return true;
    s_lastParseError = err.c_str();
    s_lastParseCode = err.code();
    logPrimaryParseFailure(path, err);
    return false;
}

bool recoverFromBak(const char *path, JsonDocument &doc) {
    char bakPath[kBakPathLen];
    if (!buildBakPath(bakPath, sizeof(bakPath), path))
        return false;
    if (!StorageDriver::fileExists(bakPath))
        return false;

    doc.clear();
    DeserializationError bakErr = StorageDriver::parseJsonFile(bakPath, doc);
    if (bakErr) {
        s_lastParseError = bakErr.c_str();
        s_lastParseCode = bakErr.code();
        LOG_ERROR("CFG", "%s also failed to parse: %s", bakPath, bakErr.c_str());
        return false;
    }

    LOG_WARN("CFG", "%s recovered from .bak", path);

    if (!StorageDriver::renameFile(bakPath, path)) {
        LOG_WARN("CFG", "Could not rename %s back to %s — non-fatal", bakPath, path);
    }
    return true;
}
}

void logLargestFreeBlock([[maybe_unused]] const char *where) {
#ifdef ARDUINO
    const uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    LOG_INFO("CFG", "heap.largest_free=%u before %s", static_cast<unsigned>(largest), where);
#endif
}

const char *lastParseError() {
    return s_lastParseError;
}

bool lastParseOutOfMemory() {
    return s_lastParseCode == DeserializationError::NoMemory;
}

bool readAndParseWithBak(const char *path, JsonDocument &doc) {
    logLargestFreeBlock(path);
    s_lastParseError = "file missing";
    s_lastParseCode = DeserializationError::Ok;
    if (parsePrimary(path, doc))
        return true;
    return recoverFromBak(path, doc);
}

bool promoteBackup(const char *path) {
    char bakPath[kBakPathLen];
    if (!buildBakPath(bakPath, sizeof(bakPath), path) || !StorageDriver::fileExists(bakPath))
        return false;
    return StorageDriver::removeFile(path) && StorageDriver::renameFile(bakPath, path);
}

void parseColor(const char *hex, CfgColor *out) {
    if (!hex || hex[0] != '#') {
        out->rgb = 0x000000;
        return;
    }
    out->rgb = static_cast<uint32_t>(strtoul(hex + 1, nullptr, 16));
}

bool decodeHexBytes(const char *hex, uint8_t *out, uint8_t maxLen, uint8_t *outLen) {
    if (!hex || !out || !outLen)
        return false;
    const size_t hexLen = strlen(hex);
    if (hexLen % 2 != 0)
        return false;
    const size_t byteLen = hexLen / 2;
    if (byteLen > maxLen)
        return false;

    for (size_t i = 0; i < byteLen; ++i) {
        char buf[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};
        char *end = nullptr;
        const unsigned long v = strtoul(buf, &end, 16);
        if (end != buf + 2 || v > 0xFFu)
            return false;
        out[i] = static_cast<uint8_t>(v);
    }
    *outLen = static_cast<uint8_t>(byteLen);
    return true;
}

}
