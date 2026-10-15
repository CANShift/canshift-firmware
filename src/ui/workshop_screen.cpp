#include "app_config.h"
#if APP_WORKSHOP_ENABLED

    #include "workshop_screen.h"

    #include "app_config.h"
    #include "diag/error_store.h"
    #include "diag/logger.h"
    #include "runtime/workshop_credentials.h"
    #include "ui/page_manager.h"
    #include "ui/severity.h"
    #include "ui/system_screen.h"
    #include "ui/theme_manager.h"
    #include "ui/widgets/control_button.h"
    #include "ui/widgets/widget_helpers.h"
    #include "workshop_mode_rs.h"

    #include <Arduino.h>
    #include <esp_system.h>
    #include <lvgl.h>

namespace WorkshopScreen {

namespace {

constexpr char kKicker[] = "WORKSHOP MODE";
constexpr char kHeadline[] = "DASH DISABLED";
constexpr char kNetworkKicker[] = "NETWORK";
constexpr char kPasswordKicker[] = "PASSWORD";
constexpr char kLeaveKicker[] = "REBOOTS THE DASH";
constexpr char kLeaveWord[] = "LEAVE";

constexpr uint8_t kHeadlinePx = 22;
constexpr uint8_t kCredentialPx = 17;
constexpr uint16_t kSingleLinePermille = 1000;
constexpr int16_t kHeadlineTopGapPx = 10;
constexpr int16_t kFootGapPx = 8;
constexpr int16_t kStackGapPx = 6;
constexpr int16_t kKickerGapPx = 3;
constexpr int16_t kLeaveMinHeightPx = 48;
constexpr int16_t kJoinCodePx = 111;
constexpr int16_t kJoinCodeQuietPx = 6;
constexpr uint32_t kJoinCodeDarkRgb = 0x000000;
constexpr uint32_t kJoinCodeLightRgb = 0xFFFFFF;

lv_obj_t *makeColumn(lv_obj_t *parent, int16_t gapPx) {
    lv_obj_t *column = lv_obj_create(parent);
    WidgetHelpers::resetContainerStyle(column);
    lv_obj_set_height(column, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(column, gapPx, LV_PART_MAIN);
    return column;
}

void addCredential(lv_obj_t *parent, const char *kicker, const char *value) {
    lv_obj_t *cell = makeColumn(parent, kKickerGapPx);
    lv_obj_set_width(cell, LV_PCT(100));
    (void)SystemScreen::addKicker(cell, kicker, ThemeManager::dimColor());
    (void)SystemScreen::addMonoLine(cell, value, ThemeManager::getEffectiveTextColor(),
                                    kCredentialPx, kSingleLinePermille);
}

void onLeave(lv_event_t *) {
    LOG_INFO("WORKSHOP", "Leaving workshop mode — rebooting");
    delay(kPreRestartFlushDelayMs);
    esp_restart();
}

void addLeaveButton(lv_obj_t *parent) {
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_width(box, LV_PCT(100));
    lv_obj_set_height(box, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(box, kLeaveMinHeightPx, LV_PART_MAIN);
    ControlButton::Surface surface = ControlButton::build(box, false);
    ControlButton::setTexts(surface, kLeaveKicker, kLeaveWord);
    ControlButton::paint(surface, ControlVocabulary::ControlState::OFF, 0);
    lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(box, onLeave, LV_EVENT_CLICKED, nullptr);
}

bool addJoinCode(lv_obj_t *parent, const WorkshopCredentials &creds) {
    char payload[kWorkshopJoinPayloadCap];
    const size_t len =
        workshop_join_payload_rs(creds.ssid, creds.password, payload, sizeof(payload));
    lv_obj_t *quiet = lv_obj_create(parent);
    WidgetHelpers::resetContainerStyle(quiet);
    lv_obj_set_size(quiet, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(quiet, lv_color_hex(kJoinCodeLightRgb), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(quiet, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(quiet, kJoinCodeQuietPx, LV_PART_MAIN);
    lv_obj_t *code = lv_qrcode_create(quiet, kJoinCodePx, lv_color_hex(kJoinCodeDarkRgb),
                                      lv_color_hex(kJoinCodeLightRgb));
    return len > 0 && code && lv_qrcode_update(code, payload, len) == LV_RES_OK;
}

bool fill(const SystemScreen::Frame &frame, const WorkshopCredentials &creds) {
    SystemScreen::setTopGap(SystemScreen::addMonoLine(frame.header.value, kHeadline,
                                                      ThemeManager::getEffectiveTextColor(),
                                                      kHeadlinePx, kSingleLinePermille),
                            kHeadlineTopGapPx);
    lv_obj_set_flex_align(frame.foot, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_t *details = makeColumn(frame.foot, kStackGapPx);
    lv_obj_set_flex_grow(details, 1);
    addCredential(details, kNetworkKicker, creds.ssid);
    addCredential(details, kPasswordKicker, creds.password);
    addLeaveButton(details);
    return addJoinCode(frame.foot, creds);
}

bool present(const WorkshopCredentials &creds) {
    const Severity::Spec spec = {Severity::Level::INFORMATION, kKicker, Severity::kRulePrimaryPx};
    const SystemScreen::FootSpec foot = {LV_FLEX_FLOW_ROW, kFootGapPx};
    const SystemScreen::Frame frame = SystemScreen::build(spec, foot);
    if (!frame.screen || !frame.foot || !fill(frame, creds)) {
        LOG_ERROR("WORKSHOP", "Workshop card could not be drawn — staying on the dash");
        ErrorStore::push(ERROR_SRC_SYSTEM, "workshop", "workshop card not drawn");
        if (frame.screen)
            lv_obj_del(frame.screen);
        return false;
    }
    SystemScreen::present(frame);
    return true;
}

} // namespace

bool enter() {
    WorkshopCredentials creds{};
    if (!WorkshopCredentialStore::loadOrCreate(creds) || !present(creds))
        return false;
    PageManager::releaseAll();
    return true;
}

} // namespace WorkshopScreen

#endif
