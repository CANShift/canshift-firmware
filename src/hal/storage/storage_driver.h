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
     * Atomic write: stage to "<path>.tmp", rotate previous "<path>" to
     * "<path>.bak", then rename ".tmp" to "<path>". On any failure the
     * original file is left untouched. Returns true on success.
     */
bool writeFileAtomic(const char *path, const uint8_t *data, size_t length);

/**
     * Check if a file exists.
     */
bool fileExists(const char *path);

/**
     * Rename src → dst. Returns true on success.
     */
bool renameFile(const char *src, const char *dst);

/**
     * Remove a file. Returns true on success or if the file did not exist.
     */
bool removeFile(const char *path);

/**
     * Return total and free space in bytes.
     */
void getSpaceInfo(size_t *totalBytes, size_t *usedBytes);

// ---------------------------------------------------------------------------
// Chunked write — single transfer in-flight at a time
//
// Used by the USB CMD_PUT_FILE command to stream large assets (banner image,
// fonts) without staging the full content in RAM. Caller orchestrates:
//
//   beginChunkedWrite("/assets/foo.bin")  // truncates / creates parent dirs
//   appendChunk(data, len)  ... repeat ...
//   endChunkedWrite()       // closes the file (returns false on finalize fail)
//
// Calling beginChunkedWrite while another transfer is open closes the prior
// one first (caller is expected to detect & handle interrupted transfers).
// ---------------------------------------------------------------------------

bool beginChunkedWrite(const char *path);

/**
     * Like beginChunkedWrite, but stages writes to "<path>.tmp" and rotates
     * the previous "<path>" to "<path>.bak" on endChunkedWrite(). The live
     * file is left untouched until the final rename succeeds.
     */
bool beginChunkedWriteAtomic(const char *path);

bool appendChunk(const uint8_t *data, size_t length);

/**
     * Close the chunked write. For atomic transfers, performs the rotate +
     * rename on close. Returns true on success; false on finalize failure
     * (the original file is restored from "<path>.bak" when possible).
     */
bool endChunkedWrite();

/**
     * Discard an in-progress chunked write without touching the live file.
     * Closes the open ".tmp" handle and removes the ".tmp" file. Safe to
     * call when no transfer is open.
     */
void abortChunkedWrite();

/**
 * Returns true if a chunked transfer is currently open.
 */
bool isChunkedWriteOpen();

/**
 * Recursively create parent directories for the given path.
 * "/a/b/c.bin" → ensures "/a" and "/a/b" exist.
 * Returns false if any intermediate mkdir fails.
 */
bool ensureParentDirs(const char *path);

/**
 * Remove an orphaned "<path>.tmp" file, if present. Used at boot to clean
 * up after a power-cut mid-write. No-op if the .tmp does not exist.
 */
void sweepOrphanTmp(const char *path);

} // namespace StorageDriver
