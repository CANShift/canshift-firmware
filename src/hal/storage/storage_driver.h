#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

namespace StorageDriver {

enum class InitStatus : uint8_t {
    NotInitialized = 0,
    Ok = 1,
    MountFailed = 2,
};

bool init();

InitStatus getStatus();

DeserializationError parseJsonFile(const char *path, JsonDocument &doc, size_t *outSize = nullptr);

size_t fileSize(const char *path);

size_t streamFileTo(const char *path, Print &out, bool replaceNewlinesWithSpaces = false);

bool writeFileAtomic(const char *path, const uint8_t *data, size_t length);

bool fileExists(const char *path);

bool renameFile(const char *src, const char *dst);

bool removeFile(const char *path);

void getSpaceInfo(size_t *totalBytes, size_t *usedBytes);

bool beginChunkedWrite(const char *path);

bool beginChunkedWriteAtomic(const char *path);

bool appendChunk(const uint8_t *data, size_t length);

bool endChunkedWrite();

void abortChunkedWrite();

bool isChunkedWriteOpen();

bool ensureParentDirs(const char *path);

void sweepOrphanTmp(const char *path);

} // namespace StorageDriver
