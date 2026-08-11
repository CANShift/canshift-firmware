#include "hal/storage/storage_driver.h"

#include <lvgl.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

namespace {

std::string s_root = "data";
FILE *s_chunked = nullptr;
std::string s_chunkedFinalPath;

std::string resolve(const char *path) {
    std::string p = s_root;
    if (path[0] != '/')
        p += '/';
    p += path;
    return p;
}

} // namespace

void simStorageSetRoot(const char *rootDir) {
    s_root = rootDir;
}

namespace StorageDriver {

bool init() {
    return true;
}

InitStatus getStatus() {
    return InitStatus::Ok;
}

DeserializationError parseJsonFile(const char *path, JsonDocument &doc, size_t *outSize) {
    FILE *f = fopen(resolve(path).c_str(), "rb");
    if (!f)
        return DeserializationError::InvalidInput;
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string buf(static_cast<size_t>(size), '\0');
    const size_t got = fread(buf.data(), 1, buf.size(), f);
    fclose(f);
    if (outSize)
        *outSize = got;
    return deserializeJson(doc, buf.data(), got);
}

size_t fileSize(const char *path) {
    struct stat st{};
    if (stat(resolve(path).c_str(), &st) != 0)
        return 0;
    return static_cast<size_t>(st.st_size);
}

size_t streamFileTo(const char *path, Print &out, bool replaceNewlinesWithSpaces) {
    FILE *f = fopen(resolve(path).c_str(), "rb");
    if (!f)
        return 0;
    size_t total = 0;
    int c;
    while ((c = fgetc(f)) != EOF) {
        uint8_t byte = static_cast<uint8_t>(c);
        if (replaceNewlinesWithSpaces && (byte == '\n' || byte == '\r'))
            byte = ' ';
        total += out.write(byte);
    }
    fclose(f);
    return total;
}

bool writeFileAtomic(const char *path, const uint8_t *data, size_t length) {
    const std::string finalPath = resolve(path);
    const std::string tmpPath = finalPath + ".tmp";
    FILE *f = fopen(tmpPath.c_str(), "wb");
    if (!f)
        return false;
    const size_t written = fwrite(data, 1, length, f);
    fclose(f);
    if (written != length)
        return false;
    return rename(tmpPath.c_str(), finalPath.c_str()) == 0;
}

bool fileExists(const char *path) {
    struct stat st{};
    return stat(resolve(path).c_str(), &st) == 0;
}

bool renameFile(const char *src, const char *dst) {
    return rename(resolve(src).c_str(), resolve(dst).c_str()) == 0;
}

bool removeFile(const char *path) {
    return remove(resolve(path).c_str()) == 0;
}

void getSpaceInfo(size_t *totalBytes, size_t *usedBytes) {
    if (totalBytes)
        *totalBytes = 832 * 1024;
    if (usedBytes)
        *usedBytes = 128 * 1024;
}

bool beginChunkedWriteAtomic(const char *path) {
    if (s_chunked)
        return false;
    s_chunkedFinalPath = resolve(path);
    s_chunked = fopen((s_chunkedFinalPath + ".tmp").c_str(), "wb");
    return s_chunked != nullptr;
}

bool appendChunk(const uint8_t *data, size_t length) {
    if (!s_chunked)
        return false;
    return fwrite(data, 1, length, s_chunked) == length;
}

bool endChunkedWrite() {
    if (!s_chunked)
        return false;
    fclose(s_chunked);
    s_chunked = nullptr;
    return rename((s_chunkedFinalPath + ".tmp").c_str(), s_chunkedFinalPath.c_str()) == 0;
}

void abortChunkedWrite() {
    if (!s_chunked)
        return;
    fclose(s_chunked);
    s_chunked = nullptr;
    remove((s_chunkedFinalPath + ".tmp").c_str());
}

bool isChunkedWriteOpen() {
    return s_chunked != nullptr;
}

bool ensureParentDirs(const char *path) {
    std::string full = resolve(path);
    for (size_t i = s_root.size() + 1; i < full.size(); ++i) {
        if (full[i] != '/')
            continue;
        const std::string dir = full.substr(0, i);
        if (mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST)
            return false;
    }
    return true;
}

void sweepOrphanTmp(const char *path) {
    remove((resolve(path) + ".tmp").c_str());
}

} // namespace StorageDriver

namespace {

void *simFsOpen(lv_fs_drv_t *, const char *path, lv_fs_mode_t mode) {
    if (mode != LV_FS_MODE_RD)
        return nullptr;
    std::string full = s_root;
    full += '/';
    full += path;
    return fopen(full.c_str(), "rb");
}

lv_fs_res_t simFsClose(lv_fs_drv_t *, void *file) {
    fclose(static_cast<FILE *>(file));
    return LV_FS_RES_OK;
}

lv_fs_res_t simFsRead(lv_fs_drv_t *, void *file, void *buf, uint32_t btr, uint32_t *br) {
    *br = static_cast<uint32_t>(fread(buf, 1, btr, static_cast<FILE *>(file)));
    return LV_FS_RES_OK;
}

lv_fs_res_t simFsSeek(lv_fs_drv_t *, void *file, uint32_t pos, lv_fs_whence_t whence) {
    const int origin = whence == LV_FS_SEEK_SET   ? SEEK_SET
                       : whence == LV_FS_SEEK_CUR ? SEEK_CUR
                                                  : SEEK_END;
    return fseek(static_cast<FILE *>(file), static_cast<long>(pos), origin) == 0
               ? LV_FS_RES_OK
               : LV_FS_RES_UNKNOWN;
}

lv_fs_res_t simFsTell(lv_fs_drv_t *, void *file, uint32_t *pos) {
    *pos = static_cast<uint32_t>(ftell(static_cast<FILE *>(file)));
    return LV_FS_RES_OK;
}

} // namespace

void simRegisterLvglFs(const char *) {
    static lv_fs_drv_t drv;
    lv_fs_drv_init(&drv);
    drv.letter = 'S';
    drv.open_cb = simFsOpen;
    drv.close_cb = simFsClose;
    drv.read_cb = simFsRead;
    drv.seek_cb = simFsSeek;
    drv.tell_cb = simFsTell;
    lv_fs_drv_register(&drv);
}
