#include "app_config.h"
#if APP_WORKSHOP_ENABLED

    #include "settings_page_internal.h"

    #include "settings_page.h"
    #include "layout_scale.h"
    #include "runtime/workshop_mode.h"
    #include "ui/widgets/widget_helpers.h"

    #include <lvgl.h>
    #include <stdint.h>

namespace SettingsPageInternal {

namespace {

lv_obj_t *s_workshopBtn = nullptr;

void onWorkshopMode(lv_event_t *) {
    if (!WorkshopMode::request()) {
        WidgetHelpers::setVisible(s_workshopBtn, false);
        return;
    }
    SettingsPage::close();
}

} // namespace

void buildWorkshopRow(int16_t &y, int16_t rowW) {
    if (!WorkshopMode::boardSupported())
        return;
    y += LayoutScale::y(GAP_ROW);
    s_workshopBtn = makeFullButton(s_panel, "WORKSHOP MODE", CLR_BTN_BG, CLR_BTN_BDR, CLR_MUTED,
                                   onWorkshopMode);
    lv_obj_set_pos(s_workshopBtn, LayoutScale::x(PAD_H), y);
    lv_obj_set_size(s_workshopBtn, rowW, BTN_H);
    y += BTN_H;
    refreshWorkshopRow();
}

void refreshWorkshopRow() {
    if (!s_workshopBtn)
        return;
    WidgetHelpers::setVisible(s_workshopBtn, WorkshopMode::offered());
}

} // namespace SettingsPageInternal

#endif
