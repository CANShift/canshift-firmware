// lvgl_fs_driver.h — LVGL file system driver backed by SPIFFS
//
// Registers drive letter 'S' with LVGL.
// Call LvglFsDriver::init() after both SPIFFS and LVGL are initialised.
// Enables lv_img_set_src() with paths like "S:/images/bg.bmp".
#pragma once

class LvglFsDriver {
public:
    static void init();

    /**
     * Idempotent variant of init(). Registers the LVGL FS driver once and
     * silently no-ops on subsequent calls. Used by SD hot-plug recovery
     * (issue #251) to re-arm the driver after a late mount without leaking
     * a second lv_fs_drv_t entry.
     */
    static void ensureRegistered();
};
