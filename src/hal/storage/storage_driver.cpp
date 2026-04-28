// storage_driver.cpp — Filesystem abstraction implementation

#include "storage_driver.h"
#include "board_config.h"
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
    return true;

#elif STORAGE_USE_SD
    if (!SD.begin(PIN_SD_CS)) {
        LOG_ERROR("STORAGE", "SD card mount failed");
        return false;
    }
    LOG_INFO("STORAGE", "SD card mounted");
    return true;
#endif
}

char* StorageDriver::readFile(const char* path, size_t* outSize) {
    File file = FS_INSTANCE.open(path, "r");
    if (!file || file.isDirectory()) {
        LOG_WARN("STORAGE", "Cannot open file: %s", path);
        if (outSize) *outSize = 0;
        return nullptr;
    }

    size_t size = file.size();
    char* buf = static_cast<char*>(malloc(size + 1));
    if (!buf) {
        LOG_ERROR("STORAGE", "malloc(%u) failed reading %s", size + 1, path);
        file.close();
        if (outSize) *outSize = 0;
        return nullptr;
    }

    size_t read = file.readBytes(buf, size);
    buf[read] = '\0';
    file.close();

    if (outSize) *outSize = read;
    LOG_DEBUG("STORAGE", "Read %u bytes from %s", read, path);
    return buf;
}

bool StorageDriver::writeFile(const char* path, const uint8_t* data, size_t length) {
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

bool StorageDriver::fileExists(const char* path) {
    return FS_INSTANCE.exists(path);
}

void StorageDriver::getSpaceInfo(size_t* totalBytes, size_t* usedBytes) {
#if STORAGE_USE_SPIFFS
    if (totalBytes) *totalBytes = SPIFFS.totalBytes();
    if (usedBytes)  *usedBytes  = SPIFFS.usedBytes();
#elif STORAGE_USE_SD
    if (totalBytes) *totalBytes = SD.totalBytes();
    if (usedBytes)  *usedBytes  = SD.usedBytes();
#endif
}
