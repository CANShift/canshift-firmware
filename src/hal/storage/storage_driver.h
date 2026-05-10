#pragma once
// storage_driver.h — SPIFFS filesystem abstraction.
//
// SPIFFS lives on the on-chip flash partition; there is no SD card on this
// board, so the storage layer never contends with the LCD/touch HSPI bus
// (see lgfx_panel.h `bus_shared = true`). If a future board adds an SD card
// on HSPI, audit that bus_shared assumption before wiring SD reads/writes.
//
// Selection is hardcoded — the firmware ships SPIFFS-only.

#include <Arduino.h>
#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

namespace StorageDriver {

// ---------------------------------------------------------------------------
// Initialization status
//
// Surfaced through getStatus() and reflected in the boot UI and the USB
// GET_STATUS response.
// ---------------------------------------------------------------------------
enum class InitStatus : uint8_t {
    NotInitialized = 0, // init() has not been called yet
    Ok = 1,             // mount succeeded; reads/writes are usable
    MountFailed = 2,    // mount failed (likely flash partition error)
};

/**
 * Initialize the filesystem.
 * Returns true on success.
 * On failure, logs an error. Caller may proceed with defaults.
 * Sets the value returned by getStatus() in all cases.
 */
bool init();

/**
 * Last init() outcome. NotInitialized until init() runs.
 */
InitStatus getStatus();

/**
 * Read an entire file into a heap-allocated buffer.
 * Caller is responsible for calling free() on the returned pointer.
 * Returns nullptr on failure. outSize is set to the file size in bytes.
 *
 * Heap pressure: this performs a single contiguous `malloc(size+1)` for the
 * whole file. For JSON parsing prefer `parseJsonFile()` instead — it streams
 * the file straight into the JsonDocument and avoids the temporary buffer.
 */
char *readFile(const char *path, size_t *outSize);

/**
 * Stream a JSON file directly into a JsonDocument without staging the full
 * file in a contiguous heap buffer first. Eliminates the readFile() malloc
 * for the common config-load path (issue #576). Returns a non-empty
 * DeserializationError on parse failure or when the file cannot be opened.
 *
 * On success, `outSize` (when non-null) is set to the file size in bytes.
 */
DeserializationError parseJsonFile(const char *path, JsonDocument &doc,
                                   size_t *outSize = nullptr);

/**
 * Return the on-disk size of a file in bytes, or 0 when the file does not
 * exist or cannot be opened. Cheaper than readFile() when the caller only
 * needs to ask "is this file empty / how large is it?".
 */
size_t fileSize(const char *path);

/**
 * Stream a file to an Arduino Print stream (e.g., Serial) in fixed-size
 * chunks, avoiding the large readFile() malloc. Returns the number of bytes
 * actually written, or 0 on open failure.
 *
 * `replaceNewlinesWithSpaces` is provided for the USB GET_CONFIG path which
 * needs a single-line response on a line-delimited wire protocol.
 */
size_t streamFileTo(const char *path, Print &out, bool replaceNewlinesWithSpaces = false);

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
