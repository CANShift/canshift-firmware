// lvgl_fs_driver.cpp — LVGL FS driver backed by SPIFFS
//
// Registers drive letter 'S' so LVGL can load BMP images from SPIFFS.
// Requires LV_USE_BMP=1 in lv_conf.h (already set).
//
// Path mapping:
//   LVGL src string:   "S:/images/bg.bmp"
//   LVGL strips "S:"   → callback receives "/images/bg.bmp"
//   Opened on SPIFFS   → SPIFFS.open("/images/bg.bmp", "r")

#include "lvgl_fs_driver.h"
#include "diag/logger.h"

#include <lvgl.h>
#include <SPIFFS.h>

// ---------------------------------------------------------------------------
// FS callbacks (static — no state other than the open File object)
// ---------------------------------------------------------------------------

namespace {

void *fs_open(lv_fs_drv_t * /*drv*/, const char *path, lv_fs_mode_t mode) {
    const char *modeStr = (mode == LV_FS_MODE_WR) ? "w" : "r";
    File *f = new File(SPIFFS.open(path, modeStr));
    if (!*f) {
        delete f;
        LOG_WARN("FS", "Cannot open: %s", path);
        return nullptr;
    }
    return f;
}

lv_fs_res_t fs_close(lv_fs_drv_t * /*drv*/, void *file_p) {
    File *f = static_cast<File *>(file_p);
    f->close();
    delete f;
    return LV_FS_RES_OK;
}

lv_fs_res_t fs_read(lv_fs_drv_t * /*drv*/, void *file_p, void *buf, uint32_t btr,
                    uint32_t *br) {
    File *f = static_cast<File *>(file_p);
    *br = f->read(static_cast<uint8_t *>(buf), btr);
    return LV_FS_RES_OK;
}

lv_fs_res_t fs_seek(lv_fs_drv_t * /*drv*/, void *file_p, uint32_t pos,
                    lv_fs_whence_t whence) {
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
