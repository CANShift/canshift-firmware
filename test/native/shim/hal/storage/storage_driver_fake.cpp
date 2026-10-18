// In-memory StorageDriver for host tests. Tests stage via fakeWrite() then
// invoke ConfigLoader; parseJsonFile() reads from the staged buffer.
#include "hal/storage/storage_driver.h"

#include "config/json_reader.h"

#include <ArduinoJson.h>
#include <string.h>
#include <string>

namespace StorageDriver {

namespace {

constexpr size_t kMaxFiles = 8;
constexpr size_t kMaxFileBytes = 64 * 1024;

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

}

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
    // Route through JsonReader::parse so the host build exercises the same
    // BoundedReader<const char*> instantiation as the device build (#1249 F-2).
    return JsonReader::parse(doc, f->data, f->size);
}

size_t fileSize(const char *path) {
    FakeFile *f = findFile(path);
    return f ? f->size : 0;
}

size_t streamFileTo(const char *path, Print &out, bool replaceNewlinesWithSpaces) {
    FakeFile *f = findFile(path);
    if (!f)
        return 0;
    for (size_t i = 0; i < f->size; ++i) {
        const bool newline = f->data[i] == '\n';
        const uint8_t byte =
            static_cast<uint8_t>(newline && replaceNewlinesWithSpaces ? ' ' : f->data[i]);
        if (out.write(byte) != 1)
            return 0;
    }
    return f->size;
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

}

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

namespace {

size_t usedByteCount() {
    size_t used = 0;
    for (const auto &f : s_files)
        if (f.used)
            used += f.size;
    return used;
}

}

void getSpaceInfo(size_t *totalBytes, size_t *usedBytes) {
    if (totalBytes)
        *totalBytes = kMaxFiles * kMaxFileBytes;
    if (usedBytes)
        *usedBytes = usedByteCount();
}

namespace {

struct ChunkedWrite {
    bool open;
    char path[64];
    uint8_t data[kMaxFileBytes];
    size_t size;
    size_t failAfter;
};

ChunkedWrite s_chunk = {false, {0}, {0}, 0, kMaxFileBytes};

}

bool beginChunkedWriteAtomic(const char *path) {
    if (!path)
        return false;
    s_chunk.open = true;
    strncpy(s_chunk.path, path, sizeof(s_chunk.path) - 1);
    s_chunk.path[sizeof(s_chunk.path) - 1] = '\0';
    s_chunk.size = 0;
    return true;
}
bool appendChunk(const uint8_t *data, size_t length) {
    if (!s_chunk.open || s_chunk.size + length > s_chunk.failAfter)
        return false;
    memcpy(s_chunk.data + s_chunk.size, data, length);
    s_chunk.size += length;
    return true;
}
bool endChunkedWrite() {
    if (!s_chunk.open)
        return true;
    s_chunk.open = false;
    return stageFileBytes(s_chunk.path, s_chunk.data, s_chunk.size);
}
ChunkCommit endChunkedWriteChecked(StagedFileCheck check) {
    if (!s_chunk.open)
        return ChunkCommit::Committed;
    s_chunk.open = false;
    const std::string staged = std::string(s_chunk.path) + ".tmp";
    const std::string backup = std::string(s_chunk.path) + ".bak";
    if (!stageFileBytes(staged.c_str(), s_chunk.data, s_chunk.size))
        return ChunkCommit::Failed;
    if (check != nullptr && !check(staged.c_str())) {
        removeFile(staged.c_str());
        return ChunkCommit::Rejected;
    }
    if (fileExists(s_chunk.path))
        renameFile(s_chunk.path, backup.c_str());
    renameFile(staged.c_str(), s_chunk.path);
    return ChunkCommit::Committed;
}
void abortChunkedWrite() {
    s_chunk.open = false;
    s_chunk.size = 0;
}
bool isChunkedWriteOpen() {
    return s_chunk.open;
}
bool ensureParentDirs(const char * /*path*/) {
    return true;
}
void sweepOrphanTmp(const char * /*path*/) {}

void fakeReset() {
    abortChunkedWrite();
    s_chunk.failAfter = kMaxFileBytes;
    for (auto &f : s_files) {
        f.used = false;
        f.size = 0;
        f.path[0] = '\0';
    }
}

void fakeWrite(const char *path, const char *contents, size_t length) {
    stageFileBytes(path, reinterpret_cast<const uint8_t *>(contents), length);
}

void fakeFailChunksAfter(size_t bytes) {
    s_chunk.failAfter = bytes;
}

const uint8_t *fakeRead(const char *path, size_t *length) {
    FakeFile *f = findFile(path);
    *length = f ? f->size : 0;
    return f ? reinterpret_cast<const uint8_t *>(f->data) : nullptr;
}

}
