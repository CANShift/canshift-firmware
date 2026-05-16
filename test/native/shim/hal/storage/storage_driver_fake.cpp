// storage_driver_fake.cpp — in-memory implementation of StorageDriver for
// host unit tests. Tests stage file contents via `fakeWrite()` before they
// invoke ConfigLoader; parseJsonFile() then deserializes from the staged buffer.
//
// The implementation deliberately keeps a fixed-size table — host tests need
// at most three config files at once, so dynamic allocation is overkill.

#include "hal/storage/storage_driver.h"

#include <ArduinoJson.h>
#include <string.h>

namespace StorageDriver {

namespace {

constexpr size_t kMaxFiles = 8;
// 32 KB is the largest CONFIG_JSON_DOC_DASHBOARD plus headroom — more than
// any host-test fixture we plan to load.
constexpr size_t kMaxFileBytes = 32 * 1024;

struct FakeFile {
    bool used;
    char path[64];
    char data[kMaxFileBytes];
    size_t size;
};

FakeFile s_files[kMaxFiles];

FakeFile *findFile(const char *path) {
    if (!path)
        return nullptr;
    for (auto &f : s_files) {
        if (f.used && strncmp(f.path, path, sizeof(f.path)) == 0)
            return &f;
    }
    return nullptr;
}

FakeFile *allocateSlot(const char *path) {
    if (!path)
        return nullptr;
    for (auto &f : s_files) {
        if (!f.used) {
            f.used = true;
            strncpy(f.path, path, sizeof(f.path) - 1);
            f.path[sizeof(f.path) - 1] = '\0';
            f.size = 0;
            return &f;
        }
    }
    return nullptr;
}

} // namespace

bool init() {
    return true;
}

InitStatus getStatus() {
    return InitStatus::Ok;
}

DeserializationError parseJsonFile(const char *path, JsonDocument &doc, size_t *outSize) {
    FakeFile *f = findFile(path);
    if (!f) {
        if (outSize)
            *outSize = 0;
        return DeserializationError::EmptyInput;
    }
    if (outSize)
        *outSize = f->size;
    return deserializeJson(doc, f->data, f->size);
}

size_t fileSize(const char *path) {
    FakeFile *f = findFile(path);
    return f ? f->size : 0;
}

size_t streamFileTo(const char *path, Print & /*out*/, bool /*replaceNewlinesWithSpaces*/) {
    // Host tests don't exercise the streaming-to-Serial path. Return size so
    // callers' contracts still see "non-zero on success".
    FakeFile *f = findFile(path);
    return f ? f->size : 0;
}

namespace {

// Internal helper shared by writeFileAtomic and the fakeWrite test stager.
// Not part of the public StorageDriver API (real driver dropped writeFile).
bool stageFileBytes(const char *path, const uint8_t *data, size_t length) {
    if (!path || length > kMaxFileBytes)
        return false;
    FakeFile *f = findFile(path);
    if (!f)
        f = allocateSlot(path);
    if (!f)
        return false;
    memcpy(f->data, data, length);
    f->size = length;
    return true;
}

} // namespace

bool writeFileAtomic(const char *path, const uint8_t *data, size_t length) {
    return stageFileBytes(path, data, length);
}

bool fileExists(const char *path) {
    return findFile(path) != nullptr;
}

bool renameFile(const char *src, const char *dst) {
    FakeFile *s = findFile(src);
    if (!s || !dst)
        return false;
    // Drop any existing destination so the rename has the same end state as
    // POSIX rename().
    FakeFile *d = findFile(dst);
    if (d) {
        d->used = false;
        d->size = 0;
    }
    strncpy(s->path, dst, sizeof(s->path) - 1);
    s->path[sizeof(s->path) - 1] = '\0';
    return true;
}

bool removeFile(const char *path) {
    FakeFile *f = findFile(path);
    if (!f)
        return true;
    f->used = false;
    f->size = 0;
    return true;
}

void getSpaceInfo(size_t *totalBytes, size_t *usedBytes) {
    if (totalBytes)
        *totalBytes = kMaxFiles * kMaxFileBytes;
    if (usedBytes) {
        size_t used = 0;
        for (const auto &f : s_files)
            if (f.used)
                used += f.size;
        *usedBytes = used;
    }
}

bool beginChunkedWrite(const char * /*path*/) {
    return false;
}
bool beginChunkedWriteAtomic(const char * /*path*/) {
    return false;
}
bool appendChunk(const uint8_t * /*data*/, size_t /*length*/) {
    return false;
}
bool endChunkedWrite() {
    return false;
}
void abortChunkedWrite() {}
bool isChunkedWriteOpen() {
    return false;
}
bool ensureParentDirs(const char * /*path*/) {
    return true;
}
void sweepOrphanTmp(const char * /*path*/) {}

void fakeReset() {
    for (auto &f : s_files) {
        f.used = false;
        f.size = 0;
        f.path[0] = '\0';
    }
}

void fakeWrite(const char *path, const char *contents, size_t length) {
    stageFileBytes(path, reinterpret_cast<const uint8_t *>(contents), length);
}

} // namespace StorageDriver
