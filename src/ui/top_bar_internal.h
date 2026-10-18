#pragma once

#include "theme_manager.h"
#include "ui/theme_tokens.h"
#include "config/config_types.h"

#include <lvgl.h>
#include <stdint.h>

namespace TopBarInternal {

constexpr uint8_t kDynTextCap = 16;

struct DynItem {
    TopBarItemKind kind;
    TopBarItemPos position;
    lv_obj_t *obj;
    char signalId[kCfgMaxSignalLen];
    char format[kDynTextCap];
    char prefix[16];

    char lastText[kDynTextCap];
    uint32_t lastColor;
    bool lastSeenValid;

    bool hidden;
    int8_t linkedFlagIdx;
    int8_t nextFlagIdx;
};

extern lv_obj_t *s_bar;
extern DynItem s_dynItems[kCfgMaxTopbarItems];
extern uint8_t s_dynCount;
extern bool s_dynEverSeen[kCfgMaxTopbarItems];
extern lv_obj_t *s_themeIcon;
extern int16_t s_height;

constexpr uint32_t kColorDotDown = 0xCC3333;
constexpr uint32_t kColorModeActive = ThemeTokens::kEngaged;
constexpr uint32_t kColorUnset = 0xFFFFFFFFu;

constexpr uint32_t kColorLabelNight = ThemeTokens::kDimNight;
constexpr uint32_t kColorLabelDay = ThemeTokens::kDimDay;
constexpr uint32_t kColorMutedNight = ThemeTokens::kDimNight;
constexpr uint32_t kColorMutedDay = ThemeTokens::kDimDay;

constexpr const char *kBusRateSilent = "— Hz";

constexpr int16_t kBarLetterSpacePx = 1;
constexpr int16_t kFlagSquarePx = 7;
constexpr int16_t kFlagGapPx = 3;

inline uint32_t labelColor() {
    return ThemeManager::pickColor(kColorLabelNight, kColorLabelDay);
}
inline uint32_t mutedColor() {
    return ThemeManager::pickColor(kColorMutedNight, kColorMutedDay);
}

}
