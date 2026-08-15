#include "no_config_screen.h"

#include "app_config.h"
#include "diag/logger.h"
#include "ui/severity.h"
#include "ui/system_screen.h"
#include "ui/theme_manager.h"
#include "ui/widgets/widget_helpers.h"

#include <lvgl.h>

namespace NoConfigScreen {

namespace {

constexpr char kKicker[] = "NO CONFIG";
constexpr char kHeadline[] = "NOTHING TO\nDISPLAY YET";
constexpr char kWhereToGo[] = "CONNECT USB-C AND OPEN\nDOCS.CANSHIFT.TMBK.CH/TUNER";
constexpr char kUsbKicker[] = "USB";
constexpr char kUsbValue[] = "READY";
constexpr char kFirmwareKicker[] = "FIRMWARE";

constexpr uint8_t kHeadlinePx = 22;
constexpr uint8_t kWhereToGoPx = 10;
constexpr uint8_t kCellValuePx = 17;
constexpr uint16_t kHeadlineLineHeightPermille = 1150;
constexpr uint16_t kWhereToGoLineHeightPermille = 1650;
constexpr uint16_t kSingleLinePermille = 1000;
constexpr int16_t kHeadlineTopGapPx = 17;
constexpr int16_t kWhereToGoTopGapPx = 11;
constexpr int16_t kCellGapPx = 6;
constexpr int16_t kCellBorderPx = 2;
constexpr int16_t kCellPadYPx = 6;
constexpr int16_t kCellPadXPx = 7;
constexpr int16_t kCellKickerGapPx = 3;

void addBody(const SystemScreen::Frame &frame) {
    SystemScreen::setTopGap(SystemScreen::addMonoLine(frame.header.value, kHeadline,
                                                      ThemeManager::getEffectiveTextColor(),
                                                      kHeadlinePx, kHeadlineLineHeightPermille),
                            kHeadlineTopGapPx);
    SystemScreen::setTopGap(SystemScreen::addMonoLine(frame.header.value, kWhereToGo,
                                                      ThemeManager::dimColor(), kWhereToGoPx,
                                                      kWhereToGoLineHeightPermille),
                            kWhereToGoTopGapPx);
}

void addCell(lv_obj_t *row, const char *kicker, const char *value) {
    lv_obj_t *cell = lv_obj_create(row);
    if (!cell)
        return;
    WidgetHelpers::resetContainerStyle(cell);
    lv_obj_set_height(cell, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(cell, 1);
    lv_obj_set_style_border_width(cell, kCellBorderPx, LV_PART_MAIN);
    lv_obj_set_style_border_color(cell, lv_color_hex(ThemeManager::getEffectiveTextColor()),
                                  LV_PART_MAIN);
    lv_obj_set_style_pad_ver(cell, kCellPadYPx, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(cell, kCellPadXPx, LV_PART_MAIN);
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(cell, kCellKickerGapPx, LV_PART_MAIN);
    (void)SystemScreen::addKicker(cell, kicker, ThemeManager::dimColor());
    (void)SystemScreen::addMonoLine(cell, value, ThemeManager::getEffectiveTextColor(),
                                    kCellValuePx, kSingleLinePermille);
}

void addCells(const SystemScreen::Frame &frame) {
    if (!frame.foot)
        return;
    addCell(frame.foot, kUsbKicker, kUsbValue);
    addCell(frame.foot, kFirmwareKicker, APP_VERSION_STR);
}

} // namespace

void show() {
    const Severity::Spec spec = {Severity::Level::INFORMATION, kKicker, Severity::kRulePrimaryPx};
    const SystemScreen::FootSpec foot = {LV_FLEX_FLOW_ROW, kCellGapPx};
    const SystemScreen::Frame frame = SystemScreen::build(spec, foot);
    if (!frame.screen) {
        LOG_ERROR("UI", "no-config screen: LVGL alloc failed");
        return;
    }
    addBody(frame);
    addCells(frame);
    SystemScreen::present(frame);
    LOG_INFO("UI", "No dashboard config — waiting for the Tuner over USB");
}

} // namespace NoConfigScreen
