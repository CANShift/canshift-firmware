#include "config_rejected_screen.h"

#include "diag/logger.h"
#include "ui/severity.h"
#include "ui/system_screen.h"
#include "ui/theme_manager.h"
#include "ui/widgets/widget_helpers.h"

#include <lvgl.h>
#include <stdio.h>

namespace ConfigRejectedScreen {

namespace {

constexpr char kKicker[] = "CONFIG REJECTED";
constexpr char kWayOut[] = "RUNNING LAST GOOD CONFIG — FIX IN THE TUNER";

constexpr uint8_t kReasonPx = 22;
constexpr uint8_t kDetailPx = 10;
constexpr uint16_t kReasonLineHeightPermille = 1100;
constexpr uint16_t kDetailLineHeightPermille = 1600;
constexpr uint16_t kSingleLinePermille = 1000;
constexpr int16_t kReasonTopGapPx = 17;
constexpr int16_t kDetailTopGapPx = 10;
constexpr int16_t kWayOutTopPadPx = 7;
constexpr int16_t kWayOutRulePx = 1;

void addReason(const SystemScreen::Frame &frame, const CfgRejection &rejection) {
    char text[48];
    snprintf(text, sizeof(text), "PAGE %u\nEXCEEDS %d PX",
             static_cast<unsigned>(rejection.pageNumber), rejection.maxY);
    SystemScreen::setTopGap(SystemScreen::addMonoLine(frame.header.value, text,
                                                      ThemeManager::getEffectiveTextColor(),
                                                      kReasonPx, kReasonLineHeightPermille),
                            kReasonTopGapPx);
}

void addDetail(const SystemScreen::Frame &frame, const CfgRejection &rejection) {
    char name[CFG_MAX_ID_LEN];
    WidgetHelpers::formatSignalLabel(rejection.widgetId, name, sizeof(name));
    const int overBy = rejection.widgetY + rejection.widgetH - rejection.maxY;
    char text[96];
    snprintf(text, sizeof(text), "WIDGET %s AT Y %d, H %d\nMAX Y %d — OVER BY %d", name,
             rejection.widgetY, rejection.widgetH, rejection.maxY, overBy);
    SystemScreen::setTopGap(SystemScreen::addMonoLine(frame.header.value, text,
                                                      ThemeManager::dimColor(), kDetailPx,
                                                      kDetailLineHeightPermille),
                            kDetailTopGapPx);
}

void addWayOut(const SystemScreen::Frame &frame) {
    if (!frame.foot)
        return;
    lv_obj_set_style_border_side(frame.foot, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_set_style_border_width(frame.foot, kWayOutRulePx, LV_PART_MAIN);
    lv_obj_set_style_border_color(frame.foot, lv_color_hex(ThemeManager::trackColor()),
                                  LV_PART_MAIN);
    lv_obj_set_style_pad_top(frame.foot, kWayOutTopPadPx, LV_PART_MAIN);
    (void)SystemScreen::addMonoLine(frame.foot, kWayOut, ThemeManager::dimColor(), kDetailPx,
                                    kSingleLinePermille);
}

} // namespace

void show(const CfgRejection &rejection) {
    const Severity::Spec spec = {Severity::Level::FAILURE, kKicker, Severity::kRulePrimaryPx};
    const SystemScreen::FootSpec foot = {LV_FLEX_FLOW_COLUMN, 0};
    const SystemScreen::Frame frame = SystemScreen::build(spec, foot);
    if (!frame.screen) {
        LOG_ERROR("UI", "config-rejected screen: LVGL alloc failed");
        return;
    }
    addReason(frame, rejection);
    addDetail(frame, rejection);
    addWayOut(frame);
    SystemScreen::present(frame);
    LOG_ERROR("UI", "Config rejected — page %u widget '%s' y=%d h=%d max=%d",
              static_cast<unsigned>(rejection.pageNumber), rejection.widgetId, rejection.widgetY,
              rejection.widgetH, rejection.maxY);
}

} // namespace ConfigRejectedScreen
