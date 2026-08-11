#include "alert_banner.h"
#include "ui/alert_sources.h"
#include "top_bar.h"
#include "ui/font_manager.h"
#include "runtime/alert_engine.h"
#include "runtime/signal_store.h"
#include "can/signal_map.h"
#include "util/format_float.h"
#include "ui/widgets/widget_helpers.h"
#include "layout_scale.h"

#include <Arduino.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

namespace {

constexpr uint32_t UPDATE_PERIOD_MS = 250;
constexpr size_t TEXT_BUF_LEN = 96;

constexpr uint32_t COL_CRIT_BG = 0xFF4444;
constexpr uint32_t COL_CRIT_TEXT = 0xFFFFFF;
constexpr uint32_t COL_MIL_BG = 0x222222;
constexpr uint32_t COL_MIL_TEXT = 0xBABABA;
constexpr uint32_t COL_LOST_BG = 0x222222;
constexpr uint32_t COL_LOST_TEXT = 0xBABABA;

constexpr int16_t CHIP_PAD_H = 8;
constexpr int16_t CHIP_PAD_V = 4;
constexpr int16_t CHIP_GAP = 6;
constexpr int16_t BANNER_TOP_GAP = 2;

lv_obj_t *s_container = nullptr;
lv_obj_t *s_critChip = nullptr;
lv_obj_t *s_critLabel = nullptr;
lv_obj_t *s_milChip = nullptr;
lv_obj_t *s_lostChip = nullptr;
lv_obj_t *s_lostLabel = nullptr;

uint32_t s_lastUpdateMs = 0;
char s_lastText[TEXT_BUF_LEN] = "";
char s_lastLostText[TEXT_BUF_LEN] = "";
bool s_lastCritVisible = false;
bool s_lastMilVisible = false;
bool s_lastLostVisible = false;

lv_obj_t *makeChip(lv_obj_t *parent, uint32_t bgRgb) {
    lv_obj_t *chip = lv_obj_create(parent);
    lv_obj_remove_style_all(chip);
    lv_obj_set_size(chip, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(chip, lv_color_hex(bgRgb), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(chip, LayoutScale::x(CHIP_PAD_H), LV_PART_MAIN);
    lv_obj_set_style_pad_ver(chip, LayoutScale::y(CHIP_PAD_V), LV_PART_MAIN);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(chip, LV_OBJ_FLAG_HIDDEN);
    return chip;
}

lv_obj_t *makeChipLabel(lv_obj_t *chip, uint32_t textRgb, uint8_t fontSize) {
    lv_obj_t *lbl = lv_label_create(chip);
    lv_obj_set_style_text_font(lbl, FontManager::label(fontSize), 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(textRgb), 0);
    lv_obj_clear_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(lbl);
    return lbl;
}

bool appendPart(char *buf, size_t len, size_t &used, const char *part) {
    const int n = snprintf(buf + used, len - used, "%s%s", (used > 0) ? " | " : "", part);
    if (n < 0 || static_cast<size_t>(n) >= len - used)
        return false;
    used += static_cast<size_t>(n);
    return true;
}

void composeCriticalText(const AlertEngine::AlertState &state, char *buf, size_t len) {
    size_t used = 0;
    buf[0] = '\0';
    for (const AlertSources::CriticalSource &src : AlertSources::kSources) {
        if (state.*(src.level) != AlertEngine::AlertLevel::CRITICAL)
            continue;
        if (state.*(src.sensorLost) && !SignalStore::isValid(src.id))
            continue;
        char valBuf[16];
        FloatFormat::formatFixed(valBuf, sizeof(valBuf), SignalStore::read(src.id), src.decimals);
        char part[32];
        snprintf(part, sizeof(part), "%s %s%s", src.chipLabel, valBuf, src.unit);
        if (!appendPart(buf, len, used, part))
            break;
    }
}

void composeSensorLostText(const AlertEngine::AlertState &state, char *buf, size_t len) {
    size_t used = 0;
    buf[0] = '\0';
    for (const AlertSources::CriticalSource &src : AlertSources::kSources) {
        if (!(state.*(src.sensorLost)))
            continue;
        char part[32];
        snprintf(part, sizeof(part), "%s SENSOR LOST", src.chipLabel);
        if (!appendPart(buf, len, used, part))
            break;
    }
}

} // namespace

void AlertBanner::init() {
    s_container = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_container);
    lv_obj_set_size(s_container, LV_HOR_RES, LV_SIZE_CONTENT);
    lv_obj_align(s_container, LV_ALIGN_TOP_MID, 0,
                 TopBar::getHeight() + LayoutScale::y(BANNER_TOP_GAP));
    lv_obj_set_flex_flow(s_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_container, LayoutScale::x(CHIP_GAP), LV_PART_MAIN);
    lv_obj_clear_flag(s_container, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_container, LV_OBJ_FLAG_HIDDEN);

    s_critChip = makeChip(s_container, COL_CRIT_BG);
    s_critLabel = makeChipLabel(s_critChip, COL_CRIT_TEXT, 14);

    s_lostChip = makeChip(s_container, COL_LOST_BG);
    s_lostLabel = makeChipLabel(s_lostChip, COL_LOST_TEXT, 12);

    s_milChip = makeChip(s_container, COL_MIL_BG);
    lv_obj_t *milLabel = makeChipLabel(s_milChip, COL_MIL_TEXT, 12);
    lv_label_set_text(milLabel, "CHECK ENGINE");

    s_lastUpdateMs = 0;
    s_lastText[0] = '\0';
    s_lastLostText[0] = '\0';
    s_lastCritVisible = false;
    s_lastMilVisible = false;
    s_lastLostVisible = false;
}

void AlertBanner::update() {
    if (!s_container)
        return;

    const uint32_t now = millis();
    if (now - s_lastUpdateMs < UPDATE_PERIOD_MS)
        return;
    s_lastUpdateMs = now;

    const AlertEngine::AlertState state = AlertEngine::getState();
    char text[TEXT_BUF_LEN];
    composeCriticalText(state, text, sizeof(text));
    char lostText[TEXT_BUF_LEN];
    composeSensorLostText(state, lostText, sizeof(lostText));
    const bool critVisible = (text[0] != '\0');
    const bool lostVisible = (lostText[0] != '\0');
    const bool milVisible = state.milActive;

    if (critVisible == s_lastCritVisible && milVisible == s_lastMilVisible &&
        lostVisible == s_lastLostVisible && strcmp(text, s_lastText) == 0 &&
        strcmp(lostText, s_lastLostText) == 0)
        return;
    s_lastCritVisible = critVisible;
    s_lastMilVisible = milVisible;
    s_lastLostVisible = lostVisible;
    strlcpy(s_lastText, text, sizeof(s_lastText));
    strlcpy(s_lastLostText, lostText, sizeof(s_lastLostText));

    if (critVisible)
        lv_label_set_text(s_critLabel, text);
    if (lostVisible)
        lv_label_set_text(s_lostLabel, lostText);
    WidgetHelpers::setVisible(s_critChip, critVisible);
    WidgetHelpers::setVisible(s_lostChip, lostVisible);
    WidgetHelpers::setVisible(s_milChip, milVisible);

    WidgetHelpers::setVisible(s_container, critVisible || lostVisible || milVisible);
}
