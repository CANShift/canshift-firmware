#pragma once

#include "config/config_types.h"
#include "ui/theme_tokens.h"
#include "ui/widget_styles.h"
#include "ui/widgets/widget_tag_pool.h"

#include <lvgl.h>
#include <stddef.h>
#include <stdint.h>

namespace WidgetHelpers {

constexpr uint32_t kZoneDangerRgb = ThemeTokens::kDanger;
constexpr uint32_t kAccentRgb = ThemeTokens::kEngaged;
constexpr uint32_t kMutedRgb = ThemeTokens::kDimNight;
constexpr uint32_t kTrackRgb = ThemeTokens::kTrackNight;

constexpr uint8_t kRulePrimaryPx = 2;
constexpr uint8_t kRuleSecondaryPx = 1;
constexpr uint8_t kRulePrimaryFontMin = 32;

constexpr int16_t kValueRightInsetPx = 8;

struct BigFontStep {
    uint8_t minBig;
    uint8_t devicePx;
};

constexpr BigFontStep kBigFontSteps[] = {{96, 48}, {88, 44}, {80, 40}, {64, 32},
                                         {48, 24}, {44, 22}, {0, 17}};

inline uint8_t deviceFontPxForBig(uint8_t big) {
    for (const BigFontStep &step : kBigFontSteps) {
        if (big >= step.minBig)
            return step.devicePx;
    }
    return 17;
}

inline int16_t valueTrackingPx(uint8_t devicePx) {
    if (devicePx >= 40)
        return -2;
    if (devicePx >= 14)
        return -1;
    return 0;
}

float clampPct(float value, float minValue, float maxValue);

void formatSignalLabel(const char *src, char *out, size_t outLen);

int formatValue(char *out, size_t outLen, const char *prefix, uint8_t decimals, float value,
                const char *suffix);

bool setLabelTextIfChanged(lv_obj_t *label, const char *text);

const char *resolveDisplayUnit(const char *signalId, const char *configSuffix);

void initContainer(lv_obj_t *cont, const CfgWidget &cfg, int16_t yOffset, bool hasBorder,
                   uint32_t borderRgb);

void resetContainerStyle(lv_obj_t *obj);

lv_obj_t *makeCircleBadge(lv_obj_t *parent, int16_t diameter, uint32_t rgb);

lv_obj_t *makeSquareBadge(lv_obj_t *parent, int16_t side, uint32_t rgb);

lv_obj_t *makeTopRule(lv_obj_t *cont, uint8_t heightPx, uint32_t rgb);

void setRuleColorIfChanged(lv_obj_t *rule, uint32_t &lastRgb, uint32_t rgb);

void reportValueOverflow(const CfgWidget &cfg, const lv_font_t *font, int16_t trackingPx,
                         const char *unit);

void logTagPoolExhausted(const char *logTag, const char *widgetId);

inline void disableInteract(lv_obj_t *obj) {
    if (!obj)
        return;
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
}

inline void setVisible(lv_obj_t *obj, bool visible) {
    if (!obj)
        return;
    if (visible) {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

// Skips the LVGL call when the flag already matches — the top-bar update path
// runs every frame and invalidation is not free.
inline void setVisibleIfChanged(lv_obj_t *obj, bool visible) {
    if (!obj)
        return;
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) == visible) {
        setVisible(obj, visible);
    }
}

// Returns nullptr when the pool is dry, after deleting `deleteOnFailure` if the
// caller has no way to render without a tag. Pass nullptr to keep the object.
template <typename T>
inline T *acquireTag(WidgetTagPool::Slot<T> &slot, const char *widgetId, const char *logTag,
                     lv_obj_t *deleteOnFailure) {
    T *tag = slot.get();
    if (tag)
        return tag;
    logTagPoolExhausted(logTag, widgetId);
    if (deleteOnFailure) {
        lv_obj_del(deleteOnFailure);
    }
    return nullptr;
}

template <typename T>
inline void attachTagDeleter(lv_obj_t *obj, T *tag) {
    if (!obj || !tag)
        return;
    lv_obj_add_event_cb(
        obj,
        [](lv_event_t *e) {
            auto *t = static_cast<T *>(lv_event_get_user_data(e));
            if (!t)
                return;
            auto *target = static_cast<lv_obj_t *>(lv_event_get_current_target(e));
            if (target)
                lv_anim_del(target, nullptr);
            WidgetTagPool::release(t);
        },
        LV_EVENT_DELETE, tag);
}

} // namespace WidgetHelpers
