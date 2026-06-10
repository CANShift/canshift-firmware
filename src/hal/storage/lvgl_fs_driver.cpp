// Registers drive 'S' for LVGL fonts/images. Requires StorageDriver::init()
// to have mounted SPIFFS first.
#include "lvgl_fs_driver.h"
#include "app_config.h"
#include "board_config.h"
#include "diag/logger.h"

#include <lvgl.h>
#include <SPIFFS.h>
#include <cstddef>
#include <esp_heap_caps.h>

namespace {

// Sized to cover the demo dashboard's icon refs + theme icons. The LVGL BIN
// decoder keeps source files OPEN for the cache lifetime — slot exhaustion
// silently breaks lv_img_set_src (#1242). SPIFFS maxOpenFiles tracks this.
constexpr size_t kFsPoolSize = 16;
File s_filePool[kFsPoolSize];
bool s_slotBusy[kFsPoolSize] = {};

// Always called under the LVGL mutex — pool bookkeeping is unsynchronised.
void *fs_open(lv_fs_drv_t * /*drv*/, const char *path, lv_fs_mode_t mode) {
    // Heap guard — newlib's __sfp() can abort() inside fopen() on fragmentation (#651).
    const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if (largest < LVGL_FS_MIN_HEAP_BYTES) {
        LOG_WARN("FS", "Refused open of %s (largest=%u) — heap too low", path, (unsigned)largest);
        return nullptr;
    }

    size_t slot = kFsPoolSize;
    for (size_t i = 0; i < kFsPoolSize; ++i) {
        if (!s_slotBusy[i]) {
            slot = i;
            break;
        }
    }
    if (slot == kFsPoolSize) {
        LOG_WARN("FS", "Open pool exhausted for %s (all %u slots busy)", path,
                 static_cast<unsigned>(kFsPoolSize));
        return nullptr;
    }

    const char *modeStr = (mode == LV_FS_MODE_WR) ? "w" : "r";
    s_filePool[slot] = SPIFFS.open(path, modeStr);
    if (!s_filePool[slot]) {
        LOG_WARN("FS", "Cannot open: %s", path);
        return nullptr;
    }
    s_slotBusy[slot] = true;
    return &s_filePool[slot];
}

lv_fs_res_t fs_close(lv_fs_drv_t * /*drv*/, void *file_p) {
    File *f = static_cast<File *>(file_p);
    f->close();
    for (size_t i = 0; i < kFsPoolSize; ++i) {
        if (&s_filePool[i] == f) {
            s_slotBusy[i] = false;
            break;
        }
    }
    return LV_FS_RES_OK;
}

lv_fs_res_t fs_read(lv_fs_drv_t * /*drv*/, void *file_p, void *buf, uint32_t btr, uint32_t *br) {
    File *f = static_cast<File *>(file_p);
    *br = f->read(static_cast<uint8_t *>(buf), btr);
    return LV_FS_RES_OK;
}

lv_fs_res_t fs_seek(lv_fs_drv_t * /*drv*/, void *file_p, uint32_t pos, lv_fs_whence_t whence) {
    File *f = static_cast<File *>(file_p);
    SeekMode sm = SeekSet;
    if (whence == LV_FS_SEEK_CUR)
        sm = SeekCur;
    else if (whence == LV_FS_SEEK_END)
        sm = SeekEnd;
    f->seek(pos, sm);
    return LV_FS_RES_OK;
}

lv_fs_res_t fs_tell(lv_fs_drv_t * /*drv*/, void *file_p, uint32_t *pos_p) {
    File *f = static_cast<File *>(file_p);
    *pos_p = f->position();
    return LV_FS_RES_OK;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void LvglFsDriver::init() {
    static lv_fs_drv_t drv;
    lv_fs_drv_init(&drv);

    drv.letter = 'S';
    drv.open_cb = fs_open;
    drv.close_cb = fs_close;
    drv.read_cb = fs_read;
    drv.seek_cb = fs_seek;
    drv.tell_cb = fs_tell;

    lv_fs_drv_register(&drv);
    LOG_INFO("FS", "LVGL SPIFFS FS driver registered (drive 'S:')");
}
