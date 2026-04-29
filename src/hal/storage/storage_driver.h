#pragma once
// storage_driver.h — Filesystem abstraction (SPIFFS or SD card)
//
// Provides a unified file read/write interface regardless of the backing store.
// Selection is controlled by STORAGE_USE_SPIFFS / STORAGE_USE_SD in board_config.h.

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

namespace StorageDriver {

/**
     * Initialize the filesystem.
     * Returns true on success.
     * On failure, logs an error. Caller may proceed with defaults.
     */
bool init();

/**
     * Read an entire file into a heap-allocated buffer.
     * Caller is responsible for calling free() on the returned pointer.
     * Returns nullptr on failure. outSize is set to the file size in bytes.
     */
char *readFile(const char *path, size_t *outSize);

/**
     * Write data to a file, replacing any existing content.
     * Returns true on success.
     */
bool writeFile(const char *path, const uint8_t *data, size_t length);

/**
     * Check if a file exists.
     */
bool fileExists(const char *path);

/**
     * Return total and free space in bytes.
     */
void getSpaceInfo(size_t *totalBytes, size_t *usedBytes);

} // namespace StorageDriver
