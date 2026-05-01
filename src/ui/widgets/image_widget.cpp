// image_widget.cpp — Static BMP image widget loaded from SPIFFS
//
// Requires the LVGL SPIFFS FS driver to be registered (lvgl_fs_driver.cpp).
// Images must be stored in SPIFFS under /images/ as 24-bit BMP files.
//
// Path stored in cfg.image.imagePath is a SPIFFS path (e.g. "/images/bg.bmp").
// This is prefixed with "S:" to form the LVGL FS path "S:/images/bg.bmp".

#include "image_widget.h"
#include "diag/logger.h"

#include <lvgl.h>
#include <string.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Tag (stores the path string used as LVGL img src)
// ---------------------------------------------------------------------------

namespace {

// Maximum LVGL path length including "S:" prefix
static constexpr size_t LVGL_PATH_LEN = 68; // 2 + CFG_MAX_PATH_LEN

struct ImageTag {
    char lvglPath[LVGL_PATH_LEN]; // e.g. "S:/images/bg.bmp"
};

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

lv_obj_t *ImageWidget::create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_pos(cont, cfg.layout.x, cfg.layout.y + yOffset);
    lv_obj_set_size(cont, cfg.layout.w, cfg.layout.h);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont, 0, LV_PART_MAIN);

    if (cfg.image.imagePath[0] == '\0') {
        LOG_WARN("IMG", "Image widget has no imagePath — skipping");
        return cont;
    }

    // Build LVGL FS path: "S:" + SPIFFS path
    // SPIFFS path already has leading slash (e.g. "/images/bg.bmp")
    ImageTag *tag = new ImageTag{};
    snprintf(tag->lvglPath, sizeof(tag->lvglPath), "S:%s", cfg.image.imagePath);

    lv_obj_t *img = lv_img_create(cont);
    lv_img_set_src(img, tag->lvglPath);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);

    // Scale image to fit widget dimensions (LVGL 8.3 integer scale factor)
    // lv_img_set_zoom uses 256 = 1:1. Scale to fit if needed.
    lv_obj_set_user_data(cont, tag);

    LOG_DEBUG("IMG", "Image widget created: %s", tag->lvglPath);
    return cont;
}
