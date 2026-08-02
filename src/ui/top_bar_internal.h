#pragma once

#include "theme_manager.h"
#include "config/config_types.h"

#include <lvgl.h>
#include <stdint.h>

namespace TopBarInternal {

constexpr uint8_t DYN_TEXT_CAP = 16;

struct DynItem {
    TopBarItemKind kind;
    lv_obj_t *obj;
    char signalId[CFG_MAX_SIGNAL_LEN];
    char format[DYN_TEXT_CAP];

    char lastText[DYN_TEXT_CAP];
    uint32_t lastColor;
    bool lastSeenValid;

    bool hidden;
    int8_t linkedFlagIdx;
    int8_t nextFlagIdx;
};

extern lv_obj_t *s_bar;
extern DynItem s_dynItems[CFG_MAX_TOPBAR_ITEMS];
extern uint8_t s_dynCount;
extern bool s_dynEverSeen[CFG_MAX_TOPBAR_ITEMS];

constexpr uint32_t COLOR_DOT_DOWN = 0xCC3333;
constexpr uint32_t COLOR_MODE_ACTIVE = 0xFF8800;
constexpr uint32_t COLOR_UNSET = 0xFFFFFFFFu;

constexpr uint32_t COLOR_LABEL_NIGHT = 0xCCCCCC;
constexpr uint32_t COLOR_LABEL_DAY = 0x000000;
constexpr uint32_t COLOR_MUTED_NIGHT = 0x666666;
constexpr uint32_t COLOR_MUTED_DAY = 0x444444;

constexpr uint8_t BAR_LABEL_FONT_PX = 10;
constexpr int16_t FLAG_SQUARE_PX = 7;
constexpr int16_t FLAG_GAP_PX = 3;

inline uint32_t labelColor() {
    return ThemeManager::pickColor(COLOR_LABEL_NIGHT, COLOR_LABEL_DAY);
}
inline uint32_t mutedColor() {
    return ThemeManager::pickColor(COLOR_MUTED_NIGHT, COLOR_MUTED_DAY);
}

} // namespace TopBarInternal
