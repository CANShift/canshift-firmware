// lvgl_fs_driver.h — LVGL file system driver backed by SPIFFS
//
// Registers drive letter 'S' with LVGL.
// Call LvglFsDriver::init() after both SPIFFS and LVGL are initialised.
// Enables lv_img_set_src() with paths like "S:/assets/icon_day.bin".
#pragma once

class LvglFsDriver {
  public:
    static void init();
};
