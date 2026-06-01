// lvgl_fs_driver.cpp — LVGL FS driver backed by SPIFFS
//
// Registers drive letter 'S' so LVGL can load fonts and images from SPIFFS.
// Requires SPIFFS to be mounted via StorageDriver::init() before calling init().
//
// Path mapping:
//   LVGL src string:   "S:/fonts/orbitron_black_32.bin"
//   LVGL strips "S:"   → callback receives "/fonts/orbitron_black_32.bin"
//   Opened on SPIFFS   → SPIFFS.open("/fonts/orbitron_black_32.bin", "r")

#include "lvgl_fs_driver.h"
#include "app_config.h"
#include "board_config.h"
#include "diag/logger.h"

#include <lvgl.h>
#include <SPIFFS.h>
#include <cstddef>
#include <esp_heap_caps.h>

// ---------------------------------------------------------------------------
// FS callbacks (static pool — no heap alloc on the LVGL load path)
// ---------------------------------------------------------------------------

namespace {

// Fixed pool of Arduino `File` slots used as the LVGL handle storage. Sized
// to cover the LVGL image cache (`LV_IMG_CACHE_DEF_SIZE = 24` in lv_conf.h)
// because the built-in BIN decoder leaves the source file OPEN for the entire
// cached lifetime of every TRUE_COLOR / TRUE_COLOR_ALPHA image — files are
// only closed when the cache evicts the entry. Sizing the pool below the
// cache size guarantees slot exhaustion as soon as more icons are preloaded
// than the pool can hold (#1242 root cause: with 19 dashboard icon refs +
// 2 theme icons against a 4-slot pool, the 5th preload onward silently fails
// in `fs_open`, the image cache returns NULL, and `lv_img_set_src` silently
// no-ops → buttons render label-only and the day/night toggle stays blank).
//
// 16 covers the demo dashboard's 14 unique icons + 2 theme icons with no
// headroom for fonts/SPA assets; bumped from 4. Each `File` wrapper is small
// (~24 B on Arduino-ESP32 — internal ref-counted descriptor pointer), so the
// 16 × 24 B = 384 B static cost is negligible. The SPIFFS-side per-open
// state is allocated by `SPIFFS.open()` on demand and freed by `f.close()`;
// SPIFFS `maxOpenFiles` is bumped to match (storage_driver.cpp).
constexpr size_t kFsPoolSize = 16;
File s_filePool[kFsPoolSize];
bool s_slotBusy[kFsPoolSize] = {};

// Always called under the LVGL mutex (UI task) — no extra synchronisation
// needed for the pool bookkeeping.
void *fs_open(lv_fs_drv_t * /*drv*/, const char *path, lv_fs_mode_t mode) {
    // Heap guard: under fragmentation, newlib's __sfp() can abort() inside
    // fopen() when extending _iob[]. Refuse the open early — LVGL treats
    // nullptr as a load failure and renders an empty image. See issue #651.
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
