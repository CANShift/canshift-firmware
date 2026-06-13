
#include "image_widget.h"
#include "ui/screen_profile.h"
#include "ui/theme_manager.h"
#include "ui/widget_styles.h"
#include "ui/widgets/widget_helpers.h"
#include "ui/widgets/widget_tag_pool.h"
#include "diag/logger.h"

#include <lvgl.h>
#include <string.h>
#include <stdio.h>

namespace {

static constexpr size_t LVGL_PATH_LEN = 2 + CFG_MAX_PATH_LEN;

struct ImageTag {
    char lvglPath[LVGL_PATH_LEN];
};

} // namespace

lv_obj_t *ImageWidget::create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    lv_obj_t *cont = lv_obj_create(parent);
    const int16_t px = ScreenProfile::scaleXVal(cfg.layout.x);
    const int16_t py = static_cast<int16_t>(ScreenProfile::scaleYVal(cfg.layout.y) + yOffset);
    lv_obj_set_pos(cont, px, py);
    lv_obj_set_size(cont, ScreenProfile::scaleXVal(cfg.layout.w),
                    ScreenProfile::scaleYVal(cfg.layout.h));
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    WidgetStyles::applyContainerBaseNoBorder(cont);

    if (cfg.image.imagePath[0] == '\0') {
        LOG_WARN("IMG", "Image widget has no imagePath — skipping");
        return cont;
    }

    WidgetTagPool::Slot<ImageTag> tagSlot;
    ImageTag *tag = tagSlot.get();
    if (!tag) {
        LOG_WARN("IMG", "Tag pool exhausted for '%s' (all %u slots busy)", cfg.id,
                 static_cast<unsigned>(WidgetTagPool::kPoolSlots));
        return cont;
    }
    snprintf(tag->lvglPath, sizeof(tag->lvglPath), "S:%s", cfg.image.imagePath);

    lv_obj_t *img = lv_img_create(cont);
    lv_img_set_src(img, tag->lvglPath);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);

    lv_obj_set_user_data(cont, tag);
    lv_obj_add_event_cb(cont, WidgetTagPool::deleteHandler<ImageTag>, LV_EVENT_DELETE,
                        tagSlot.commit());

    LOG_DEBUG("IMG", "Image widget created: %s", tag->lvglPath);
    return cont;
}
